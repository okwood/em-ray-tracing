#include "LinearAcc.h"
#include "KdTreeAcc.h"
#include "GridAcc.h"

#include "Triangle.h"
#include "Sphere.h"
#include "Ray.h"
#include "Matrix.h"
#include "Complex.h"
#include "RxFields.h"

#include "Utils.h"
#include "Engine.h"

// Scene
std::vector<Geometry *> scene;

// Preprocessing
Accelerator *accelerator = NULL;

// Tx pointhy
Point txPoint;
double txPower;

// Rx points
std::vector<Point> rxPoints;
std::vector<RxFields> rxFields;
double rxRadius;

// Other parameters
struct RtParameter
{
    // user specified
    double permittivity;
    double conductivity;
    int maxReflections;
    double raySpacing;   // unit: degree
    double frequency;    // unit: MHz

    // automatic
    double lamda;
    double k;
} parameters;

void Initialize()
{
    Utils::StartTimer();
    Utils::PrintTime("Initialize");

    scene.clear();
}

void AddTriangle(const RtTriangle &triangle)
{
    Point a = Point(triangle.a.x, triangle.a.y, triangle.a.z);
    Point b = Point(triangle.b.x, triangle.b.y, triangle.b.z);
    Point c = Point(triangle.c.x, triangle.c.y, triangle.c.z);
    Vector n = Vector(triangle.n.x, triangle.n.y, triangle.n.z);

    Triangle *t = new Triangle(a, b, c, n);
    scene.push_back(t);
}

void AddTriangles(const RtTriangle *triangles, int n)
{
    for (int i = 0; i < n; i++)
    {
        Point a = Point(triangles[i].a.x, triangles[i].a.y, triangles[i].a.z);
        Point b = Point(triangles[i].b.x, triangles[i].b.y, triangles[i].b.z);
        Point c = Point(triangles[i].c.x, triangles[i].c.y, triangles[i].c.z);
        Vector n = Vector(triangles[i].n.x, triangles[i].n.y, triangles[i].n.z);

        Triangle *t = new Triangle(a, b, c, n);
        scene.push_back(t);
    }
}

bool AddStlModel(const char *filename)
{
    // Open file
    FILE *fp = NULL;
    if (fopen_s(&fp, filename, "rb") != 0)
    {
        fprintf(stderr, "Error: Cannot open file \"%s\"\n", filename);
        return false;
    }

    // Read header and count
    char header[80];
    int count = -1;

    fread(header, 80, 1, fp);
    fread(&count, sizeof(int), 1, fp);

    // Read triangles
    for (int i = 0; i < count; i++)
    {
        float nx, ny, nz;
        float ax, ay, az;
        float bx, by, bz;
        float cx, cy, cz;
        short attribute;

        fread(&nx, sizeof(float), 1, fp);
        fread(&ny, sizeof(float), 1, fp);
        fread(&nz, sizeof(float), 1, fp);

        fread(&ax, sizeof(float), 1, fp);
        fread(&ay, sizeof(float), 1, fp);
        fread(&az, sizeof(float), 1, fp);

        fread(&bx, sizeof(float), 1, fp);
        fread(&by, sizeof(float), 1, fp);
        fread(&bz, sizeof(float), 1, fp);

        fread(&cx, sizeof(float), 1, fp);
        fread(&cy, sizeof(float), 1, fp);
        fread(&cz, sizeof(float), 1, fp);
        
        fread(&attribute, sizeof(short), 1, fp);

        Vector normal = Vector((double)nx, (double)ny, (double)nz);
        Point a = Point((double)ax, (double)ay, (double)az);
        Point b = Point((double)bx, (double)by, (double)bz);
        Point c = Point((double)cx, (double)cy, (double)cz);

        // Add to scene
        scene.push_back(new Triangle(a, b, c, normal));
    }

    return true;
}

bool SetPreprocessMethod(RtPreprocessMethod method)
{
    if (method == Linear)
    {
        accelerator = new LinearAcc(&scene);
        fprintf(stderr, "    Preprocess method: Linear\n");
    }
    else if (method == Grid)
    {
        accelerator = new GridAcc(&scene);
        fprintf(stderr, "    Preprocess method: Grid\n");
    }
    else if (method == KdTree)
    {
        accelerator = new KdTreeAcc(&scene);
        fprintf(stderr, "    Preprocess method: Kd-tree\n");
    }
    else
    {
        fprintf(stderr, "Error: Unknown preprocess method\n");
        return false;
    }

    return true;
}

void SetTxPoint(const RtPoint &point, double power)
{
    txPoint = Point(point.x, point.y, point.z);
    txPower = power;
    fprintf(stderr, "    Tx Point: (%.2lf, %.2lf, %.2lf)\n", point.x, point.y, point.z);
    fprintf(stderr, "    Tx Power: %.2lf dBm\n", power);
}

void SetRxPoints(const RtPoint *points, int n, double radius)
{
    rxPoints.clear();
    for (int i = 0; i < n; i++)
    {
        rxPoints.push_back(Point(points[i].x, points[i].y, points[i].z));
    }
    rxRadius = radius;
    fprintf(stderr, "    Number of Rx Points: %d\n", n);
    fprintf(stderr, "    Rx Sphere Radius = %.2lf\n", radius);
}

void SetParameters(
    double permittivity, double conductivity,  int maxReflections,
    double raySpacing, double frequency)
{
    parameters.permittivity = permittivity;
    parameters.conductivity = conductivity;
    parameters.maxReflections = maxReflections;
    parameters.raySpacing = raySpacing;
    parameters.frequency = frequency;

    fprintf(stderr, "    Parameters:\n");
    fprintf(stderr, "      - Permittivity: %.2lf\n", permittivity);
    fprintf(stderr, "      - Conductivity: %.5lf\n", conductivity);
    fprintf(stderr, "      - Max reflections: %d\n", maxReflections);
    fprintf(stderr, "      - Ray spacing: %.3lf degrees\n", raySpacing);
    fprintf(stderr, "      - Frequency: %.1lf\n", frequency);
}

void calc_fresnel_coeff(double psi, ComplexNumber &RH, ComplexNumber &RV)
{
    ComplexNumber epsilon( // complex relative permittivity
        parameters.permittivity, 
        -60.0 * parameters.lamda * parameters.conductivity);
    ComplexNumber eta = (epsilon - cos(psi) * cos(psi)).Sqrt();
    RH = (epsilon * sin(psi) - eta) / (epsilon * sin(psi) + eta);
    RV = (ComplexNumber(sin(psi), 0) - eta) / (ComplexNumber(sin(psi), 0) + eta);
}

void calc_new_base(const Vector &axi, const Vector &axr,
                   Vector &alpha1, Vector &beta1, Vector &alpha2, Vector &beta2)
{
    alpha1 = axi.cross(axr).norm();
    if (fabs(alpha1.x) < 0.00001 &&
        fabs(alpha1.y) < 0.00001 &&
        fabs(alpha1.z) < 0.00001) // perpendicular to the wall
    {
        alpha1 = fabs(axi.x) > 0.1 ? 
            Vector(0, 1, 0).cross(axi).norm() : Vector(1, 0, 0).cross(axi).norm();
    }
    beta1 = axi.cross(alpha1).norm();
    alpha2 = alpha1;
    beta2 = axr.cross(alpha2).norm();
}

void calc_new_base(const Vector &dir, Vector &alpha, Vector &beta)
{
    alpha = fabs(dir.x) > 0.1 ? 
        Vector(0, 1, 0).cross(dir) : 
        Vector(1, 0, 0).cross(dir);
    beta = dir.cross(alpha);
}

ComplexVector calc_field_direct(Ray &r, double distance)
{
    // 20dBm: 0.1 W / 100 mW
    // 10dBm: 0.01 W / 10 mW
    //  0dBm: 0.001 W / 1 mW
    double Pt = pow(10, txPower / 10.0 - 3.0); // Watt
    double eta0 = 377; // Ohm

    Vector phi_v = Vector(0, 0, 1).cross(r.direction);
    Vector theta_v = phi_v.cross(r.direction);

    // suppose vertical polar
    double E_theta_mag = sqrt(Pt * eta0 / (2 * PI)) / distance;
    double E_theta_phase = -parameters.k * distance;
    double E_phi_mag = 0;
    double E_phi_phase = 0;

    // complex field
    ComplexNumber E_theta = ComplexNumber::Euler(E_theta_mag, E_theta_phase);
    ComplexNumber E_phi   = ComplexNumber::Euler(E_phi_mag, E_phi_phase);

    // complex field vector
    ComplexVector Ez = E_theta * theta_v + E_phi * phi_v;
    return Ez;
}

ComplexVector calc_field_direct(Ray &r, double distance, const ComplexVector &Ei)
{
    // New base
    Vector alpha(0, 0, 0);
    Vector beta(0, 0, 0);
    calc_new_base(r.direction, alpha, beta);

    // h * A = unit * Ei
    //     A = inv(h) * unit * Ei
    //
    // h:    3x3 matrix (new base)
    // unit: 3x3 unit matrix
    // A:    input complex field in new coordinate system
    // Ei:   input complex field
    Matrix h(
        alpha.x, beta.x, r.direction.x,
        alpha.y, beta.y, r.direction.y,
        alpha.z, beta.z, r.direction.z);
    Matrix inv = h.inverse();
    Matrix unit(
        1, 0, 0,
        0, 1, 0,
        0, 0, 1);
    ComplexVector A = inv * (unit * Ei);

    // Reflection field
    ComplexNumber E_alpha(0, 0);
    ComplexNumber E_beta(0, 0);

    if (r.state == Ray::MoreReflect)
    {
        // spherical wave diffusion factor (Ars2 = s1 / (s1 + s2))
        double factor = r.prev_mileage / (r.prev_mileage + distance);

        E_alpha = A.x * ComplexNumber::Euler(factor, -parameters.k * distance);
        E_beta = A.y * ComplexNumber::Euler(factor, -parameters.k * distance);
    }
    else
    {
        fprintf(stderr, "Error: invalid ray state in calc_field_reflect\n");
    }

    ComplexVector Ez = E_alpha * alpha + E_beta * beta;
    return Ez;
}

ComplexVector calc_field_reflect(Ray &r, const IntersectResult &result, const ComplexVector &Ei)
{
    const Vector &n = result.normal; // points to the outside
    Vector nl = (n.dot(r.direction) < 0) ? n : n * -1; // points to the ray

    Vector axi = r.direction;
    Vector axr = r.direction - nl * 2 * nl.dot(r.direction);

    // Glancing angle
    double psi = acos(axi.dot(axr)) / 2.0;

    // Reflection Coeff
    ComplexNumber RH(0, 0); // horizontal polar
    ComplexNumber RV(0, 0); // vertical polar
    calc_fresnel_coeff(psi, RH, RV);

    // New base
    Vector alpha1(0, 0, 0);
    Vector beta1(0, 0, 0);
    Vector alpha2(0, 0, 0);
    Vector beta2(0, 0, 0);
    calc_new_base(axi, axr, alpha1, beta1, alpha2, beta2);

    // h * A = unit * Ei
    //     A = inv(h) * unit * Ei
    //
    // h:    3x3 matrix (new base)
    // unit: 3x3 unit matrix
    // A:    input complex field in new coordinate system
    // Ei:   input complex field
    Matrix h(
        alpha1.x, beta1.x, axi.x,
        alpha1.y, beta1.y, axi.y,
        alpha1.z, beta1.z, axi.z);
    Matrix inv = h.inverse();
    Matrix unit(
        1, 0, 0,
        0, 1, 0,
        0, 0, 1);
    ComplexVector A = inv * (unit * Ei);

    // Reflection field
    ComplexNumber E_alpha(0, 0);
    ComplexNumber E_beta(0, 0);
    if (r.state == Ray::FirstReflect)
    {
        E_alpha = A.x * RV * 1.0; // the amplitude and the phase remains the same (1.0)
        E_beta = A.y * RH * 1.0;
    }
    else if (r.state == Ray::MoreReflect)
    {
        // from previous point to current interseciton point
        double s2 = Vector(r.prev_point, result.position).length();

        // spherical wave diffusion factor (Ars2 = s1 / (s1 + s2))
        double factor = r.prev_mileage / (r.prev_mileage + s2);

        E_alpha = A.x * RV * ComplexNumber::Euler(factor, -parameters.k * s2);
        E_beta = A.y * RH * ComplexNumber::Euler(factor, -parameters.k * s2);
    }
    else
    {
        fprintf(stderr, "Error: invalid ray state in calc_field_reflect\n");
    }

    ComplexVector Er = E_alpha * alpha2 + E_beta * beta2;
    return Er;
}

double calc_power(const ComplexVector &E)
{
    double norm_sqr = 
        E.x.a * E.x.a + E.x.b * E.x.b +
        E.y.a * E.y.a + E.y.b * E.y.b +
        E.z.a * E.z.a + E.z.b * E.z.b;
    double watt = parameters.lamda * parameters.lamda / (8.0 * PI * 377.0) * norm_sqr;
    return 10 * log10(watt) + 30.0; // dBm
}

double calc_sphere_area(double theta1, double theta2, double phi1, double phi2)
{
    // Area = int_theta1_theta2(int_phi1_phi2(r^2 * sin(phi) * d(phi) * d(theta))
    //      = int_theta1_theta2(int_phi1_phi2(sin(phi) * d(phi)) * d(theta))
    //      = int_theta1_theta2(-cos(phi2) + cos(phi1)) * d(theta)
    //      = (theta2 - theta1) * [cos(phi1) - cos(phi2)]
    return (theta2 - theta1) * (cos(phi1) - cos(phi2));
}

void trace(Ray &r, int depth, const ComplexVector &E) // trace with initial field
{
    std::vector<RxIntersection> rxSpheres;
    IntersectResult result = accelerator->intersect(r, rxSpheres);

    if (depth > parameters.maxReflections)
        return;

    if (!rxSpheres.empty()) // intersect with rx spheres
    {
        for (unsigned int i = 0; i < rxSpheres.size(); i++)
        {
            // Calculate field
            ComplexVector Ez = calc_field_direct(r, rxSpheres[i].distance, E);

            // Calculate zoom factor
            double mileage = r.prev_mileage + rxSpheres[i].distance;
            double projectionArea = r.unit_surface_area * mileage * mileage;
            double rxSphereArea = PI * rxSpheres[i].radius * rxSpheres[i].radius;
            if (projectionArea < rxSphereArea)
                Ez = Ez * sqrt(projectionArea / rxSphereArea);

            // Add to field list
            rxFields[rxSpheres[i].index].AddField(Ez, r.path, rxSpheres[i].offset);
        }
    }

    if (result.hit) // intersect with triangle
    {
        if (r.state == Ray::MoreReflect) // tx -> r -> triangle (will reflect again)
        {
            // Calculate input field
            ComplexVector Ei = calc_field_direct(r, result.distance, E);

            // Calculate reflection field
            ComplexVector Er = calc_field_reflect(r, result, Ei);

            // Trace recursively
            Vector n = result.normal; // points to the outside
            Vector nl = (n.dot(r.direction) < 0) ? n : n * -1; // points to the ray
            Vector v = r.direction - nl * 2 * nl.dot(r.direction);

            Ray newRay(result.position, v, r.unit_surface_area);
            newRay.state = Ray::MoreReflect; // State is still "MoreReflect"
            newRay.prev_point = result.position;
            newRay.prev_mileage = r.prev_mileage + result.distance;
            newRay.path.addPoint(result.geometry->index);

            trace(newRay, depth + 1, Er);
        }
        else
        {
            fprintf(stderr, "Error: invalid ray state in trace(r, depth, E)\n");
        }
    }
    else
    {
        return;
    }
}

void trace(Ray &r, int depth)
{
    std::vector<RxIntersection> rxSpheres;
    IntersectResult result = accelerator->intersect(r, rxSpheres);

    if (!rxSpheres.empty()) // intersect with rx spheres
    {
        if (r.state == Ray::Start) // tx -> rx sphere (direct)
        {
            for (unsigned int i = 0; i < rxSpheres.size(); i++)
            {
                // Calculate field
                ComplexVector Ez = calc_field_direct(r, rxSpheres[i].distance);

                // Add to field list
                rxFields[rxSpheres[i].index].AddField(Ez, r.path, rxSpheres[i].offset);
            }
        }
    }

    if (result.hit) // intersect with triangle
    {
        if (r.state == Ray::Start) // tx -> triangle (will reflect)
        {
            // Calculate input field
            ComplexVector Ei = calc_field_direct(r, result.distance);

            // Update state
            r.state = Ray::FirstReflect;

            // Calculate reflection field
            ComplexVector Er = calc_field_reflect(r, result, Ei);

            // Trace recursively
            Vector n = result.normal; // points to the outside
            Vector nl = (n.dot(r.direction) < 0) ? n : n * -1; // points to the ray
            Vector v = r.direction - nl * 2 * nl.dot(r.direction);

            Ray newRay(result.position, v, r.unit_surface_area);
            newRay.state = Ray::MoreReflect; // Update state
            newRay.prev_point = result.position;
            newRay.prev_mileage = Vector(r.origin, result.position).length();
            newRay.path.addPoint(result.geometry->index);

            trace(newRay, depth + 1, Er);
        }
    }
    else
    {
        return; // out of the scene
    }
}

bool Simulate() 
{
    // Add rx spheres (to scene)
    for (unsigned int i = 0; i < rxPoints.size(); i++)
    {
        scene.push_back(new RxSphere(rxPoints[i], rxRadius, i));

        // Initialize containers for fields
        rxFields.push_back(RxFields());
    }

    // Calculate automatic parameters
    parameters.lamda = 299792458.0 / (parameters.frequency * 1000000.0); // lamda = c / f
    parameters.k = 2 * PI / parameters.lamda;

    // Preprocess
    Utils::PrintTime("Preprocessing started");
    accelerator->init();
    Utils::PrintTime("Preprocessing finished");

    // TODO: print warning messages
    //       when other parameters have not been specified

    // Generate rays
    // example: ray spacing = 60 degree
    //        0     60    120   180   240   300
    // theta: o-----o-----o-----o-----o-----o-----x
    //           30    90    150
    // phi:   ---o-----o-----o---
    int nTheta = (int)(360.0 / parameters.raySpacing + 0.5);
    int nPhi = (int)(180.0 / parameters.raySpacing + 0.5);

    for (int i = 0; i < nTheta; i++)
    {
        for (int j = 0; j < nPhi; j++)
        {
            double theta = i * PI * 2.0 / nTheta;
            double phi = (j + 0.5) * PI / nPhi;

            double theta1 = i * PI * 2.0 / nTheta;
            double theta2 = (i + 1) * PI * 2.0 / nTheta;
            double phi1 = j * PI / nPhi;
            double phi2 = (j + 1) * PI / nPhi;
            double unitSufaceArea = calc_sphere_area(theta1, theta2, phi1, phi2);

            Ray ray(txPoint, Vector(sin(phi) * cos(theta), sin(phi) * sin(theta), cos(phi)), unitSufaceArea);
            trace(ray, 0);
        }
        fprintf(stderr, "\rSimulating [%d / %d]", i + 1, nTheta);
    }
    fprintf(stderr, "\n");
    Utils::PrintTime("Sinulation finished");

    return true;
}

void GetRxPowers(double *powers, int n)
{
    for (unsigned int i = 0; i < rxFields.size(); i++) // for each rx point
    {
        ComplexVector sum = rxFields[i].Sum();
        if (sum.x.a == 0 && sum.x.b == 0 &&
            sum.y.a == 0 && sum.y.b == 0 &&
            sum.z.a == 0 && sum.z.b == 0)
        {
            powers[i] = txPower - 250.0;
        }
        else
        {
            powers[i] = calc_power(sum);
        }
    }
}
