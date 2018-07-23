#if defined(_MSC_VER)
 // Make MS math.h define M_PI
 #define _USE_MATH_DEFINES
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "BitmapFontClass.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <linmath.h>
#include <windows.h>

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>

static GLfloat alpha = 210.f, beta = -70.f;
static GLfloat zoom = 8.f;

static double cursorX;
static double cursorY;

static void debug_print(const char *s, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, s);
    vsnprintf(buf, sizeof(buf), s, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

struct Vector
{
    float x;
    float y;
    float z;

    bool operator == (const Vector &other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }

    Vector operator += (const Vector &other)
    {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    Vector operator + (const Vector &other) const
    {
        Vector tmp = *this;
        tmp += other;
        return tmp;
    }

    Vector operator -= (const Vector &other)
    {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    Vector operator - (const Vector &other) const
    {
        Vector tmp = *this;
        tmp -= other;
        return tmp;
    }

    Vector operator - () const
    {
        Vector res;
        res.x = -x;
        res.y = -y;
        res.z = -z;
        return res;
    }

    float length() const
    {
        return sqrtf(x* x + y * y + z * z);
    }

    Vector &operator *= (float f)
    {
        x *= f;
        y *= f;
        z *= f;
        return *this;
    }

    Vector operator * (float f) const
    {
        Vector tmp = *this;
        tmp *= f;
        return tmp;
    }

    Vector &operator /= (float f)
    {
        x /= f;
        y /= f;
        z /= f;
        return *this;
    }

    Vector operator / (float f) const
    {
        Vector tmp = *this;
        tmp /= f;
        return tmp;
    }

    Vector &normalize()
    {
        *this /= length();
        return *this;
    }
};

Vector cross(Vector a, Vector b)
{
    Vector tmp;
    tmp.x = a.y * b.z - a.z * b.y;
    tmp.y = a.z * b.x - a.x * b.z;
    tmp.z = a.x * b.y - a.y * b.x;
    return tmp;
}

float dot(Vector a, Vector b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

struct Ray
{
    Vector pt;
    Vector dir;

    float distance_along(Vector p) const
    {
        return dot(p - pt, dir);
    }

    float distance_from(Vector p) const
    {
        return (p - (pt + dir * distance_along(p))).length();
    }
};

struct Box
{
    float xmin, xmax, ymin, ymax, zmin, zmax;

    Vector center() const
    {
        Vector c;
        c.x = (xmax + xmin) / 2.0f;
        c.y = (ymax + ymin) / 2.0f;
        c.z = (zmax + zmin) / 2.0f;
        return c;
    }

    float size() const
    {
        return (std::max)(xmax - xmin, (std::max)(ymax - ymin, zmax - zmin));
    }

    Box &operator += (const Box &other)
    {
        if (other.xmax > xmax)
            xmax = other.xmax;
        if (other.xmin < xmin)
            xmin = other.xmin;
        if (other.ymax > ymax)
            ymax = other.ymax;
        if (other.ymin < ymin)
            ymin = other.ymin;
        if (other.zmax > zmax)
            zmax = other.zmax;
        if (other.zmin < zmin)
            zmin = other.zmin;
        return *this;
    }

    Box operator + (const Box &other) const
    {
        Box r = *this;
        r += other;
        return r;
    }
};

struct VertexRecord
{
    Vector point;
    Vector normal;
    bool operator == (const VertexRecord &other) const
    {
        return point == other.point && normal == other.normal;
    }
};

struct Record
{
    float normal[3];
    Vector vertex1;
    Vector vertex2;
    Vector vertex3;
    unsigned short attribs;
};

struct Hasher
{
    unsigned int operator() (const VertexRecord &r) const
    {
        return operator()(r.point);
    }

    unsigned int operator() (const Vector &v) const
    {
        return *(unsigned int *)&v.x ^ * (unsigned int *)&v.y ^ * (unsigned int *)&v.z;
    }
};

struct Color
{
    float r, g, b;
};

struct Mesh
{
    std::vector<Vector> vertices;
    std::vector<Vector> normals;
    typedef std::unordered_map<VertexRecord, unsigned int, Hasher> IndexMap;
    IndexMap indices;
    std::vector<unsigned int> triangles;
    std::vector<unsigned int> edges;
    Vector center;
    Color color;
    Box box;
    bool box_cached = false;
    bool include_in_scene_box = true;

    Mesh()
    {
        color.r = 0.8f;
        color.g = 0.7f;
        color.b = 0.6f;
    }

    unsigned int get_index(const Vector &v, const Vector &n)
    {
        VertexRecord r = { v, n };
        auto it = indices.find(r);
        if (it != indices.end())
            return it->second;
        box_cached = false;
        indices.insert(std::make_pair(r, (int)indices.size()));
        vertices.push_back(v);
        normals.push_back(n);
        return (int)vertices.size() - 1;
    }

    void read_stl(const char *filename)
    {
        FILE *fp = fopen(filename, "rb");
        if (fp == nullptr)
            return;
        char buf[80];
        fread(buf, 1, 6, fp);

        if (_strnicmp(buf, "solid ", 6) == 0)
            read_ascii_stl(fp);
        else
        {
            fread(buf, 1, 74, fp);
            read_binary_stl(fp);
        }
        fclose(fp);
        make_edges();
    }

    void read_token(FILE *fp, char *token, size_t max_token)
    {
        int n = 0;
        for (;;)
        {
            int c = fgetc(fp);
            if (c == EOF || c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                if (c != EOF && n == 0)
                    continue;
                break;
            }
            token[n++] = c;
        }
        token[n] = '\0';
    }

    bool parse_real(const char *token, float *f)
    {
        char *end;
        double d = strtod(token, &end);
        if (*end == '\0')
        {
            if (d > FLT_MAX || d < -FLT_MAX)
                return false;
            *f = (float)d;
            return true;
        }
        return false;
    }

    bool read_ascii_stl(FILE *fp)
    {
        int state = 0;
        Record r;
        bool first = true;
        for (;;)
        {
            char token[512];
            read_token(fp, token, sizeof(token));
            if (token[0] == '\0')
                break;
            int oldstate = state;
            switch (state)
            {
            case 0:
                if (strcmp(token, "facet") == 0) ++state;
                if (strcmp(token, "endsolid") == 0)
                    return true;
                break;
            case 1:
                if (strcmp(token, "normal") == 0) ++state;
                break;
            case 2:
                if (parse_real(token, &r.normal[0])) ++state;
                break;
            case 3:
                if (parse_real(token, &r.normal[1])) ++state;
                break;
            case 4:
                if (parse_real(token, &r.normal[2])) ++state;
                break;
            case 5:
                if (strcmp(token, "outer") == 0) ++state;
                break;
            case 6:
                if (strcmp(token, "loop") == 0) ++state;
                break;
            case 7:
                if (strcmp(token, "vertex") == 0) ++state;
                break;
            case 8:
                if (parse_real(token, &r.vertex1.x)) ++state;
                break;
            case 9:
                if (parse_real(token, &r.vertex1.y)) ++state;
                break;
            case 10:
                if (parse_real(token, &r.vertex1.z)) ++state;
                break;
            case 11:
                if (strcmp(token, "vertex") == 0) ++state;
                break;
            case 12:
                if (parse_real(token, &r.vertex2.x)) ++state;
                break;
            case 13:
                if (parse_real(token, &r.vertex2.y)) ++state;
                break;
            case 14:
                if (parse_real(token, &r.vertex2.z)) ++state;
                break;
            case 15:
                if (strcmp(token, "vertex") == 0) ++state;
                break;
            case 16:
                if (parse_real(token, &r.vertex3.x)) ++state;
                break;
            case 17:
                if (parse_real(token, &r.vertex3.y)) ++state;
                break;
            case 18:
                if (parse_real(token, &r.vertex3.z)) ++state;
                break;
            case 19:
                if (strcmp(token, "endloop") == 0) ++state;
                break;
            case 20:
                if (strcmp(token, "endfacet") == 0)
                {

                    Vector a = r.vertex2 - r.vertex1;
                    Vector b = r.vertex3 - r.vertex1;
                    Vector n = cross(a, b);
                    n /= n.length();
                    unsigned int i1 = get_index(r.vertex1, n);
                    unsigned int i2 = get_index(r.vertex2, n);
                    unsigned int i3 = get_index(r.vertex3, n);
                    triangles.push_back(i1);
                    triangles.push_back(i2);
                    triangles.push_back(i3);
                    state = 0;
                }
                break;
            }

            if (!first && oldstate == state)
            {
                debug_print("unrecognize token %s\n", token);
                return false; // unrecognized token or out of order token
            }
            first = false;
        }
        return true;
    }

    bool read_binary_stl(FILE *fp)
    {
        unsigned int n_triangles;
        if (fread(&n_triangles, sizeof(unsigned int), 1, fp) != 1)
            return false;
        for (unsigned int i = 0; i < n_triangles; ++i)
        {
            Record r;
            size_t n_read = fread(&r, 1, 50, fp);
            if (n_read <= 0)
                break;
            if (n_read != 50)
                return false;
            Vector a = r.vertex2 - r.vertex1;
            Vector b = r.vertex3 - r.vertex1;
            Vector n = cross(a, b);
            if (n.length() == 0.0f)
                continue;
            n /= n.length();
            unsigned int i1 = get_index(r.vertex1, n);
            unsigned int i2 = get_index(r.vertex2, n);
            unsigned int i3 = get_index(r.vertex3, n);
            triangles.push_back(i1);
            triangles.push_back(i2);
            triangles.push_back(i3);
        }
        return true;
    }

    Box model_box()
    {
        if (box_cached)
            return box;
        box_cached = true;
        if (vertices.empty())
        {
            box = Box{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
            return box;
        }

        float xmax = vertices[0].x, xmin = vertices[0].x;
        float ymax = vertices[0].y, ymin = vertices[0].y;
        float zmax = vertices[0].z, zmin = vertices[0].z;

        for (size_t i = 1; i < vertices.size(); ++i)
        {
            if (vertices[i].x < xmin)
                xmin = vertices[i].x;
            if (vertices[i].y < ymin)
                ymin = vertices[i].y;
            if (vertices[i].x < zmin)
                zmin = vertices[i].z;
            if (vertices[i].x > xmax)
                xmax = vertices[i].x;
            if (vertices[i].y > ymax)
                ymax = vertices[i].y;
            if (vertices[i].x > zmax)
                zmax = vertices[i].z;
        }

        box = Box{ xmin, xmax, ymin, ymax, zmin, zmax };
        return box;
    }

    void render(bool wireframe)
    {
        if (vertices.empty())
            return;

        glColor3f(color.r, color.g, color.b);
        glVertexPointer(3, GL_FLOAT, sizeof(struct Vector), &vertices[0]);
        glNormalPointer(GL_FLOAT, sizeof(struct Vector), &normals[0]);
        if (wireframe)
        {
            glDrawElements(GL_LINES, (int)edges.size(), GL_UNSIGNED_INT, &edges[0]);
        }
        else
        {
            glDrawElements(GL_TRIANGLES, (int)triangles.size(), GL_UNSIGNED_INT, &triangles[0]);
        }
    }

    void clear()
    {
        box_cached = false;
        vertices.clear();
        normals.clear();
        indices.clear();
        triangles.clear();
        edges.clear();
    }

    uint64_t make_edge_id(unsigned int a, unsigned int b)
    {
        if (a < b)
            return ((uint64_t)a << 32) | b;
        else
            return ((uint64_t)b << 32) | a;
    }

    void make_edges()
    {
        std::unordered_set<uint64_t> edge_ids;
        for (size_t i = 0; i < triangles.size(); i += 3)
        {
            for (int e = 0; e < 3; ++e)
            {
                unsigned int v1 = triangles[i + e];
                unsigned int v2 = triangles[i + (e == 2 ? 0 : e + 1)];
                uint64_t eid = make_edge_id(v1, v2);
                if (edge_ids.count(eid) == 0)
                {
                    edge_ids.insert(eid);
                    edges.push_back(v1);
                    edges.push_back(v2);
                }
            }
        }
    }

    Vector sphere_pt(float r, float u, float v)
    {
        Vector pt = { cos(u) * sin(v) * r, cos(v) * r, sin(u) * sin(v) * r };
        return pt;
    }

    void sphere_triangle(float r, Vector v1, Vector v2, Vector v3, Vector c)
    {
        Vector n1 = v1 / r;
        Vector n2 = v2 / r;
        Vector n3 = v3 / r;
        unsigned int i1 = get_index(v1 + c, n1);
        unsigned int i2 = get_index(v2 + c, n2);
        unsigned int i3 = get_index(v3 + c, n3);
        triangles.push_back(i1);
        triangles.push_back(i2);
        triangles.push_back(i3);
    }

    void make_sphere(float r, Vector c)
    {
        float startU = 0;
        float startV = 0;
        float endU = (float)M_PI * 2;
        float endV = (float)M_PI;
        int u_steps = 32;
        int v_steps = 32;
        float stepU = (endU - startU) / u_steps; // step size between U-points on the grid
        float stepV = (endV - startV) / v_steps; // step size between V-points on the grid
        for (int i = 0; i < u_steps; ++i) 
        { 
            for (int j = 0; j< v_steps; ++j) 
            { 
                // V-points
                float u = i * stepU + startU;
                float v = j * stepV + startV;
                float un = (i + 1 == u_steps) ? endU : (i + 1)*stepU + startU;
                float vn = (j + 1 == v_steps) ? endV : (j + 1)*stepV + startV;
                Vector p0 = sphere_pt(r, u, v);
                Vector p1 = sphere_pt(r, u, vn);
                Vector p2 = sphere_pt(r, un, v);
                Vector p3 = sphere_pt(r, un, vn);
                sphere_triangle(r, p0, p2, p1, c);
                sphere_triangle(r, p3, p1, p2, c);
            }
        }
        make_edges();
    }
};

//========================================================================
// Draw scene
//========================================================================

static void print_matrix(const char *msg, mat4x4 m)
{
    debug_print("%s\n", msg);
    for (int i = 0; i < 4; ++i)
        debug_print("%g %g %g %g\n", m[i][0], m[i][1], m[i][2], m[i][3]);
}

struct Scene
{
    GLFWwindow *window;
    mat4x4 modelview;
    mat4x4 projection;
    Vector center;
    float scale = 1.0f;
    bool perspective = true;
    double mouse_down_x, mouse_down_y;
    bool dragged = false;
    bool wireframe = false;

    std::vector<std::unique_ptr<Mesh>> objects;

    std::string message;
    CBitmapFont font;

    void draw();
    void init_opengl();
    void key_callback(int key, int scancode, int action, int mods);
    void mouse_button_callback(int button, int action, int mods);
    void cursor_position_callback(double mouse_x, double mouse_y);
    bool pick(double mouse_x, double mouse_y, Vector &v);
    void scroll_callback(double x, double y);
    void set_projection();
    bool fire_point(const Ray &ray, Vector &v);
    bool fire_line(const Ray &ray, Vector &v);
    void autoscale();
};

static Scene scene;

void Scene::autoscale()
{
    scale = 1.0f;
    center = { 0.0f, 0.0f, 0.0f };

    if (!objects.empty())
    {
        Box b;
        bool first = true;
        for (size_t i = 0; i < objects.size(); ++i)
        {
            if (!objects[i]->include_in_scene_box)
                continue;
            if (first)
            {
                b = objects[i]->model_box();
                first = false;
            }
            else
                b += objects[i]->model_box();
        }
        scale = 2.0f / b.size();
        center = b.center();
    }
}

void Scene::draw()
{
    GLfloat mat_ambient_color[] = { 0.8f, 0.8f, 0.8f, 1.0f };

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    GLfloat position[] = { 1.0f, 1.0f, perspective ? 0.0f : 3.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, position);

#if 0
    // Move back
    glTranslatef(0.0, 0.0, -zoom);
    glScalef(scale, scale, scale);
    glRotatef(beta, 1.0, 0.0, 0.0);
    glRotatef(alpha, 0.0, 0.0, 1.0);
    glTranslatef(-m.center.x, -m.center.y, -m.center.z);
#endif

    mat4x4_identity(modelview);
    float s = scale;
    if (perspective)
        mat4x4_translate_in_place(modelview, 0.0f, 0.0f, -zoom);
    else
        s *= zoom / 8.0f;
    mat4x4_scale_aniso(modelview, modelview, s, s, s);
    mat4x4_rotate_X(modelview, modelview, beta / 180.0f * (float)M_PI);
    mat4x4_rotate_Z(modelview, modelview, alpha / 180.0f * (float)M_PI);
    mat4x4_translate_in_place(modelview, -center.x, -center.y, -center.z);

    glLoadMatrixf((const GLfloat *)modelview);

    //glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_ambient_color);

    for (const auto &m : objects)
        m->render(wireframe);

    if (!message.empty())
    {
        GLfloat ambientLight1[] = { 0.2f, 0.2f, 0.2f, 1.0f };
        GLfloat ambientLight2[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight2);
        font.ezPrint(message.c_str(), 25, 50);
        glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight1);
    }
    glfwSwapBuffers(window);
}


//========================================================================
// Initialize Miscellaneous OpenGL state
//========================================================================

void Scene::init_opengl()
{
    // Create light components
    GLfloat ambientLight[] = { 0.2f, 0.2f, 0.2f, 1.0f };
    GLfloat diffuseLight[] = { 0.8f, 0.8f, 0.8f, 1.0f };
    GLfloat specularLight[] = { 0.5f, 0.5f, 0.5f, 1.0f };
    GLfloat position[] = { 1.0f, 1.0f, 0.0f, 1.0f };

    // Assign created components to GL_LIGHT0
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuseLight);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specularLight);
    //glLightfv(GL_LIGHT0, GL_POSITION, position);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    // Use Gouraud (smooth) shading
    glShadeModel(GL_SMOOTH);

    // Switch on the z-buffer
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_RESCALE_NORMAL);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);

    //glEnableClientState(GL_COLOR_ARRAY);
    //glVertexPointer(3, GL_FLOAT, sizeof(struct Vertex), vertex);
    //glColorPointer(3, GL_FLOAT, sizeof(struct Vertex), &vertex[0].r); // Pointer to the first color

    //glPointSize(2.0);

    // Background color is black
    glClearColor(0.2f, 0.2f, 0.4f, 0.f);
}


//========================================================================
// Print errors
//========================================================================

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}

//========================================================================
// Handle key strokes
//========================================================================

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    scene.key_callback(key, scancode, action, mods);
}

void Scene::key_callback(int key, int scancode, int action, int mods)
{
    if (action != GLFW_PRESS)
        return;

    switch (key)
    {
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_SPACE:
            break;
        case GLFW_KEY_LEFT:
            alpha += 5;
            break;
        case GLFW_KEY_RIGHT:
            alpha -= 5;
            break;
        case GLFW_KEY_UP:
            beta -= 5;
            break;
        case GLFW_KEY_DOWN:
            beta += 5;
            break;
        case GLFW_KEY_PAGE_UP:
            zoom -= 0.25f;
            if (zoom < 0.f)
                zoom = 0.f;
            break;
        case GLFW_KEY_PAGE_DOWN:
            zoom += 0.25f;
            break;
        case GLFW_KEY_O:
            perspective = false;
            set_projection();
            break;
        case GLFW_KEY_P:
            perspective = true;
            set_projection();
            break;
        case GLFW_KEY_T:
            alpha = 0.0f;
            beta = 0.0f;
            break;
        case GLFW_KEY_W:
            wireframe = !wireframe;
            break;
        default:
            break;
    }
}

//========================================================================
// Callback function for mouse button events
//========================================================================

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    scene.mouse_button_callback(button, action, mods);
}

void Scene::mouse_button_callback(int button, int action, int mods)
{
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;

    if (action == GLFW_PRESS)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwGetCursorPos(window, &cursorX, &cursorY);
        mouse_down_x = cursorX;
        mouse_down_y = cursorY;
        dragged = false;
    }
    else
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        debug_print("down %g %g up %g %g\n", mouse_down_x, mouse_down_y, x, y);
        if (!dragged && !objects.empty())
        {
            Vector c;
            if (pick(x, y, c))
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "Pick: (%7.3f,%7.3f,%7.3f)", c.x, c.y, c.z);
                message = buf;
                debug_print("%s\n", message.c_str());
                objects.back()->clear();
                objects.back()->make_sphere(0.02f / scale, c);
                objects.back()->color = Color{ 0.8f, 0.8f, 0.8f };
            }
            else
            {
                message.clear();
            }
        }
    }
}


//========================================================================
// Callback function for cursor motion events
//========================================================================
static void cursor_position_callback(GLFWwindow* window, double mouse_x, double mouse_y)
{
    scene.cursor_position_callback(mouse_x, mouse_y);
}

void Scene::cursor_position_callback(double mouse_x, double mouse_y)
{
    //Vector v;
    //pick(mouse_x, mouse_y, v);

    if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
    {
        if (std::abs(mouse_x - cursorX) > 5.f || std::abs(mouse_y - cursorY) > 5.f)
            dragged = true;
        alpha += (GLfloat) (mouse_x - cursorX) / 10.f;
        beta += (GLfloat) (mouse_y - cursorY) / 10.f;

        cursorX = mouse_x;
        cursorY = mouse_y;
    }
}

bool Scene::pick(double mouse_x, double mouse_y, Vector &v)
{
    int width;
    int height;
    glfwGetWindowSize(window, &width, &height);
    float x = (2.0f * (float)mouse_x) / width - 1.0f;
    float y = 1.0f - (2.0f * (float)mouse_y) / height;
    debug_print("cursor %g %g => %g %g\n", mouse_x, mouse_y, x, y);
    mat4x4 inverse;
    mat4x4_invert(inverse, projection);
    vec4 normMouse = { x, y, 1.0f, 1.0f };
    vec4 dir = { 0.0f, 0.0f, 1.0f, 0.0f };
    vec4 pos;
    vec4 eye = { 0.0f, 0.0f, 0.0f, 1.0f };
    mat4x4_mul_vec4(pos, inverse, normMouse);
    mat4x4_mul_vec4(eye, inverse, eye);
    debug_print("pos %g %g %g\n", pos[0], pos[1], pos[2]);
    mat4x4_mul_vec4(dir, inverse, dir);
    debug_print("dir %g %g %g\n", dir[0], dir[1], dir[2]);

    mat4x4_invert(inverse, modelview);
    vec4 pt;
    mat4x4_mul_vec4(pt, inverse, pos);
    mat4x4_mul_vec4(eye, inverse, eye);
    debug_print("pt %g %g %g\n", pt[0], pt[1], pt[2]);
    mat4x4_mul_vec4(dir, inverse, dir);
    debug_print("dir %g %g %g\n", dir[0], dir[1], dir[2]);
    debug_print("eye %g %g %g\n", eye[0], eye[1], eye[2]);

    Ray r = { { pt[0], pt[1], pt[2] }, Vector{ dir[0], dir[1], dir[2] }.normalize() };
    return fire_line(r, v);
}

bool Scene::fire_point(const Ray &ray, Vector &v)
{
    Vector nearest;
    float dist = FLT_MAX;
    bool first = true;

    for (const auto &m : objects)
    {
        if (!m->include_in_scene_box)
            continue;
        for (const auto &pt : m->vertices)
        {
            float f = ray.distance_from(pt);
            if (f > 0.3f)
                continue;
            float d = ray.distance_along(pt);
            if (first)
            {
                first = false;
                dist = d;
                nearest = pt;
            }
            else if (d < dist)
            {
                dist = d;
                nearest = pt;
            }
        }
    }

    if (first)
        debug_print("no nearest\n");
    else
    {
        debug_print("nearest %g %g %g\n", nearest.x, nearest.y, nearest.z);
        v = nearest;
    }

    return !first;
}

bool Scene::fire_line(const Ray &ray, Vector &v)
{
    Vector nearest;
    float dist = FLT_MAX;
    bool first = true;

    for (const auto &m : objects)
    {
        if (!m->include_in_scene_box)
            continue;

        const Vector *vertices = &m->vertices[0];
        const unsigned int  *triangles = &m->triangles[0];

        for (size_t i = 0; i < m->triangles.size(); i += 3)
        {
            Vector side1 = vertices[triangles[i + 1]] - vertices[triangles[i]];
            Vector side2 = vertices[triangles[i + 2]] - vertices[triangles[i]];
            Vector triNorm = cross(side1, side2).normalize();
            float d = dot(triNorm, ray.dir);
            Vector w = ray.pt - vertices[triangles[i]];
            float s = dot(triNorm, w) / d;
            Vector intx = ray.pt - ray.dir * s;
            float atot = cross(side1, side2).length() / 2.0f;
            float ax1 = cross(intx - vertices[triangles[i]], side2).length() / 2.0f;
            float ax2 = cross(intx - vertices[triangles[i + 1]], -side1).length() / 2.0f;
            float ax3 = cross(intx - vertices[triangles[i + 2]], vertices[triangles[i + 1]] - vertices[triangles[i + 2]]).length() / 2.0f;
            //debug_print("pt %g %g %g\n", intx.x, intx.y, intx.z);
            if (fabs(atot - ax1 - ax2 - ax3) < atot * 1e-6)
            {
                if (first)
                {
                    first = false;
                    dist = s;
                    nearest = intx;
                }
                else if (s > dist)
                {
                    dist = s;
                    nearest = intx;
                }
            }
        }
    }

    if (first)
        debug_print("no nearest\n");
    else
    {
        debug_print("nearest %g %g %g\n", nearest.x, nearest.y, nearest.z);
        v = nearest;
    }

    return !first;
}

//========================================================================
// Callback function for scroll events
//========================================================================

static void scroll_callback(GLFWwindow* window, double x, double y)
{
    scene.scroll_callback(x, y);
}

void Scene::scroll_callback(double x, double y)
{
    zoom += (float) y / 4.f;
    if (zoom < 0)
        zoom = 0;
}

void Scene::set_projection()
{
    int width;
    int height;
    glfwGetWindowSize(window, &width, &height);

    float ratio = 1.f;

    if (height > 0)
        ratio = (float)width / (float)height;

    //debug_print("view port %d %d\n", width, height);

    // Setup viewport
    glViewport(0, 0, width, height);

    // Change to the projection matrix and set our viewing volume
    glMatrixMode(GL_PROJECTION);
    if (perspective)
        mat4x4_perspective(projection,
            60.f * (float)M_PI / 180.f,
            ratio,
            1.f, 1024.f);
    else
        mat4x4_ortho(projection, -ratio, ratio, -1, 1, -10, 10);
    glLoadMatrixf((const GLfloat*)projection);
}

static void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    scene.set_projection();
}

static void make_indicator()
{
    std::unique_ptr<Mesh> m = std::make_unique<Mesh>();
    m->make_sphere(0.5f, Vector{ 15.0f, 15.0f, 15.0f });
    m->color = Color{ 0.8f, 0.8f, 0.8f };
    m->include_in_scene_box = false;
    scene.objects.push_back(std::move(m));
}

static void drop_callback(GLFWwindow *window, int n_files, const char** files)
{
    scene.objects.clear();

    for (int i = 0; i < n_files; ++i)
    {
        std::unique_ptr<Mesh> m = std::make_unique<Mesh>();
        m->read_stl(files[i]);
        scene.objects.push_back(std::move(m));
    }

    make_indicator();
    scene.autoscale();
}

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    const char *filename = nullptr;

    if (__argc > 1)
        filename = __argv[1];

    GLFWwindow* window;
    int width, height;

    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
        exit(EXIT_FAILURE);

    window = glfwCreateWindow(640, 480, "3D Viewer", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    scene.window = window;
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetDropCallback(window, drop_callback);

    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc) glfwGetProcAddress);
    glfwSwapInterval(1);

    glfwGetFramebufferSize(window, &width, &height);
    framebuffer_size_callback(window, width, height);

    scene.font.Load("D:\\Projects\\3dview\\Times New Roman.bff");

    if (filename != nullptr)
    {
        if (strcmp(filename, "-spheres") == 0)
        {
            for (int i = 0; i < 8; ++i)
            {
                std::unique_ptr<Mesh> m = std::make_unique<Mesh>();
                Vector c = { (i & 1) * 10.0f, ((i & 2) >> 1) * 10.0f, ((i & 4) >> 2) * 10.0f };
                m->make_sphere(5.0f, c);
                if ((i & 1) != 0)
                    m->color = Color{ 0.9f, 0.2f, 0.2f };
                if (i == 7)
                    m->color = Color{ 0.2f, 0.9f, 0.2f };
                scene.objects.push_back(std::move(m));
            }
        }
        else
        {
            std::unique_ptr<Mesh> m = std::make_unique<Mesh>();
            m->read_stl(filename);
            scene.objects.push_back(std::move(m));
        }
        make_indicator();
    }

    scene.init_opengl();
    scene.autoscale();

    while (!glfwWindowShouldClose(window))
    {
        scene.draw();

        glfwWaitEventsTimeout(0.1);
        //alpha += 5;
        //glfwPollEvents();
    }

    glfwTerminate();
    exit(EXIT_SUCCESS);
}

