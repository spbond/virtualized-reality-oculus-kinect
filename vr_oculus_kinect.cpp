/* Author: Shaun Bond (samuraicodemonkey@gmail.com)
 * Date:   4-20-2015
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses,
 * or the following URLs:
 * http://www.apache.org/licenses/LICENSE-2.0
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * If you redistribute this file in source form, modified or unmodified, you
 * may:
 *   1) Leave this header intact and distribute it under the same terms,
 *      accompanying it with the APACHE20 and GPL20 files, or
 *   2) Delete the Apache 2.0 clause and accompany it with the GPL2 file, or
 *   3) Delete the GPL v2 clause and accompany it with the APACHE20 file
 * In all cases you must keep the copyright notice intact and include a copy
 * of the CONTRIB file.
 *
 * Binary distributions must follow the binary distribution requirements of
 * either License.
 */

// NOTE: Oculus daemon must be running (may have to be manually started)
// NOTE: Kinect and Oculus position tracking camera must be on different buses

#include "libfreenect.hpp"
#include "lib/glslprog.h"
#include <pthread.h>

#include <iostream>
using std::cerr;
using std::cout;
using std::endl;

#include <string>
using std::string;

#include <vector>
using std::vector;

#include <algorithm>
using std::copy;

#include <iomanip>
using std::setw;
using std::fixed;
using std::setprecision;

#include "lib/vec4.h"
#include "OVR.h"

#include "OVR_CAPI_GL.h"
using OVR::Matrix4f;
using OVR::Vector3f;

#include "Service_NetClient.h" // So I can query Oculus service for HMD errors

#if defined(__APPLE__)
    #include <GLUT/glut.h>
    #include <OpenGL/gl.h>
    #include <OpenGL/glu.h>
#else
    #include <GL/glut.h>
    #include <GL/gl.h>
    #include <GL/glu.h>
#endif



// Global constants
const short IMG_WIDTH = 640;
const short IMG_HEIGHT = 480;
const short PXL_SIZE = 3;
const short DIMENSIONS = 3;
const short INVALID_DEPTH = 2047;
const char  ESC = 27;

// Global variables
int saved_x = 0;
int saved_y = 0;

// Dimensions of render texture
unsigned texture_w = 0;
unsigned texture_h = 0;

vector<unsigned> indices;   // Vertex indices for triangle strip.
vector<float> texCoords;    // Texture coordinates for triangle strip.

ovrHmd hmd = NULL;
ovrEyeRenderDesc eyeRenderDesc[2];
ovrGLTexture eyeTextures[2];

GLuint hide_invalid_vertices = 0;
GLuint gl_rgb_tex;
GLuint eye_tex[2];
GLuint frame_buffers[2];

double freenect_angle(0);
int window(0);
int g_argc;
char **g_argv;



/*
  Coordinate system:
    Origin is Kinect's IR receiver
    +X faces to the left (from Kinect's point of view)
    +Y is up
    +Z is away from Kinect
    
  Assumes that depth image is 640 x 480

  This class is curtesy of Dr. Orion Lawlor.  Used here in original form.
*/
class kinect_depth_image {
public:
  kinect_depth_image(const uint16_t *d_)
  : depthi(d_), w(IMG_WIDTH), h(IMG_HEIGHT)
  {
    pixelFOV=tan(0.5 * (M_PI / 180.0) * 57.8)/(w*0.5);
  }

  /* Return depth, in meters, at this pixel */
  float depth(int x,int y) const {
    uint16_t disp=depthi[y*w+x];
    if (disp>= INVALID_DEPTH) return 0.0;

    //From Stephane Magnenat's depth-to-distance conversion function:
    return 0.1236 * tan(disp / 2842.5 + 1.1863) - 0.037; // (meters)
  }

  /* Return 3D direction pointing from the sensor out through this pixel
       (not a unit vector) */
  vec3 dir(int x,int y) const {
    // Ypix = -Ydist / (pixelFOV*Depth) + .5h
    return vec3((x-w*0.5)*pixelFOV, (h*0.5-y)*pixelFOV, 1);
  }

  /* Return 3D location, in meters, at this pixel */
  vec3 loc(int x,int y) const {
    // Project view ray out for that pixel
    return dir(x,y)*depth(x,y);
  }

private:
  const uint16_t *depthi;
  int w, h;       /* dimensions of image */
  float pixelFOV; /* Unit-depth field of view offset per X or Y pixel */
};

/* Borrowed this class from cppview.cpp.  Used here in original form. */
class Mutex {
public:
    Mutex() {
        pthread_mutex_init( &m_mutex, NULL );
    }
    void lock() {
        pthread_mutex_lock( &m_mutex );
    }
    void unlock() {
        pthread_mutex_unlock( &m_mutex );
    }

    class ScopedLock
    {
        Mutex & _mutex;
    public:
        ScopedLock(Mutex & mutex)
    : _mutex(mutex)
    {
            _mutex.lock();
    }
        ~ScopedLock()
        {
            _mutex.unlock();
        }
    };
private:
    pthread_mutex_t m_mutex;
};


/* Borrowed this class from cppview.cpp. Used here in a heavily modified form */
class MyFreenectDevice : public Freenect::FreenectDevice {
public:
    enum DisplayMode {POINTS, TRIANGLES};

    MyFreenectDevice(freenect_context *_ctx, int _index)
    : Freenect::FreenectDevice(_ctx, _index),
          m_buffer_video(freenect_find_video_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_VIDEO_RGB).bytes),
          m_new_rgb_frame(false),
          m_new_vertices(false),
          m_display_format(TRIANGLES),
          m_depth_frames(0)
    {}

    ~MyFreenectDevice() {
        stopVideo();
        stopDepth();
    }

    // Do not call directly even in child
    void VideoCallback(void* _rgb, uint32_t timestamp) {
        Mutex::ScopedLock lock(m_rgb_mutex);
        uint8_t* rgb = static_cast<uint8_t*>(_rgb);
        copy(rgb, rgb+getVideoBufferSize(), m_buffer_video.begin());
        m_new_rgb_frame = true;
    };

    // Do not call directly even in child
    // Recieves a depth image for processing.
    // Stores grayscale image in m_buffr_depth, with greater distance = darker.
    // Stores 3d vertex for each pixel in m_vertices.
    // Sets m_new_depth_frame and m_new_vertices to true.
    void DepthCallback(void* _depth, uint32_t timestamp) {
        Mutex::ScopedLock vertexLock(m_vertex_mutex);

        uint16_t* depth = static_cast<uint16_t*>(_depth);
        kinect_depth_image img(depth);

        // Move last frame into m_vertices.
        m_vertices.clear();

        // Convert every other row and every other column into vertices.
        for( unsigned int yy = 0 ; yy < IMG_HEIGHT ; yy+=2) {
            for( unsigned int xx = 0 ; xx < IMG_WIDTH ; xx+=2) {

                // Get 3d coordinates of pixel in meters
                vec3 vertex = img.loc(xx, yy);

                // Push vertex onto vertex array.
                m_vertices.push_back( vertex.x );
                m_vertices.push_back( vertex.y );
                m_vertices.push_back( vertex.z );
            }
        }

        m_new_vertices = true;
        m_depth_frames += 1;
    }

    // If no new rgb frame
    //    Returns false
    //    buffer remains unchanged.
    //    m_new_rgb_frame remains unchanged.
    // Otherwise
    //    Returns true
    //    buffer will contain rgb image acuired from Kinect.
    //    Sets m_new_rgb_frame to false;
    bool getRGBframe(vector<uint8_t> &buffer) {
        Mutex::ScopedLock lock(m_rgb_mutex);

        if (!m_new_rgb_frame)
            return false;

        buffer.swap(m_buffer_video);
        m_new_rgb_frame = false;
        return true;
    }


	// If no new depth frame
	//    Returns false
	//    buffer remains unchanged.
	//    m_new_vertices remains unchanged.
	// Otherwise
	//    Returns true
	//    buffer will contain 3d vertices acuired from depth image.
	//    Sets m_new_vertices to false;
    bool getVertices(vector<float> &buffer) {
        Mutex::ScopedLock lock(m_vertex_mutex);

        if (!m_new_vertices)
            return false;

        buffer.swap(m_vertices);
        m_new_vertices = false;
        return true;
    }

    // Returns the currently set display format.
    DisplayMode getDisplayMode() {
        return m_display_format;
    }

    // Returns the number of frames which have been processed.
    unsigned getFrames() {
        return m_depth_frames;
    }

    // Toggles display mode between 3d point cloud, and kinect video.
    void toggleDisplayMode() {
        if (m_display_format == TRIANGLES)
            m_display_format = POINTS;
        else if (m_display_format == POINTS)
            m_display_format = TRIANGLES;
    }

private:
    vector<uint8_t>  m_buffer_video;
    vector<float> m_vertices;
    Mutex m_rgb_mutex;
    Mutex m_vertex_mutex;
    bool m_new_rgb_frame;
    bool m_new_vertices;
    DisplayMode m_display_format;
    unsigned m_depth_frames;
};


Freenect::Freenect freenect;
MyFreenectDevice* device;


// This function is called every frame to track FPS statistics.
void calculateFPS()
{
    // Variables used for various framerate calculations
    static const unsigned NUM_FRAMES = 30;
    static unsigned frames = 0;
    static double saved_time[NUM_FRAMES] = {0};
    static double avg_fps = 0;
    static double max_fps = 0;
    static double min_fps = DBL_MAX;
    
    // Used to access time NUM_FRAMES frames ago
    unsigned index = frames % NUM_FRAMES;
    
    // Get elapsed time in seconds and record the current time
    double curr_time = glutGet(GLUT_ELAPSED_TIME)/ 1000.0;
    double elapsed_time = curr_time - saved_time[index];
    saved_time[index] = curr_time;
    
    // Calculate current fps averaged over NUM_FRAMES frames for stability,
    // average fps over total execution time, and min and max fps.
    // First NUM_FRAMES frames don't yield accurate information.
    double fps = NUM_FRAMES / elapsed_time;
    if( frames > NUM_FRAMES - 1 ) // At least NUM_FRAMES frames have passed
    {
        float x = 1.0 / (frames - (NUM_FRAMES - 1)); // 1 / number of fpses in average
        avg_fps = (1 - x) * avg_fps + x * fps; 

        if( fps > max_fps ) max_fps = fps;
        if( fps < min_fps ) min_fps = fps;

        // Here is some console output for user
        device->updateState();
        cout << "\r  demanded tilt angle: " << setw(5) << freenect_angle
             <<    " device tilt angle: "   << setw(5) << device->getState().getTiltDegs()
             << fixed << setprecision(2)
             << " fps: "     << setw(6) << fps
             << " avg fps: " << setw(6) << avg_fps
             << " min fps: " << setw(6) << min_fps
             << " max fps: " << setw(6) << max_fps
             << " kinect fps: " << setw(6) << device->getFrames() / curr_time;
        cout.flush();
    }

    ++frames;
}


// Sets up rendering parameters for kinect image vertices
void setUpVertices(void* vertices)
{
    // Send vertices to the graphics card
    glVertexPointer(3,
                    GL_FLOAT,
                    3*sizeof(float),
                    vertices );

    glEnableClientState(GL_VERTEX_ARRAY);
}


// This function is responsible for rendering the scene every frame
void DrawGLScene()
{
    // Set up buffers for images and point cloud
    static vector<uint8_t> rgb(IMG_WIDTH * IMG_HEIGHT * PXL_SIZE);
    static vector<float> vertices(IMG_WIDTH * IMG_HEIGHT * DIMENSIONS / 4);

    calculateFPS();

    // Start rendering. This allows libOVR to track timing information
    // for things like predictive position tracking, which helps with rendering.
    ovrHmd_BeginFrame(hmd, 0);

    // set viewport
    glViewport(0, 0, texture_w, texture_h);

    // Get the offset of each eye from center.
    ovrVector3f hmdToEyeViewOffset[2];
    hmdToEyeViewOffset[0] = eyeRenderDesc[0].HmdToEyeViewOffset;
    hmdToEyeViewOffset[1] = eyeRenderDesc[1].HmdToEyeViewOffset;

    // Position and orientation of each eye will be stored in eyePoses.
    ovrPosef eyePoses[2];
    ovrTrackingState hmdState;
    ovrHmd_GetEyePoses(hmd, 0, hmdToEyeViewOffset, eyePoses, &hmdState);

    if(!(hmdState.StatusFlags & ovrStatus_PositionTracked))
        cout << endl << "No position tracking" << endl;

    if(!(hmdState.StatusFlags & ovrStatus_PositionConnected))
        cout << endl << "Position tracker not connected" << endl;

    // Get the geometry.
    device->getVertices(vertices);
    setUpVertices(&vertices.front());

    // Setup the texture to place on geometry.
    device->getRGBframe(rgb);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl_rgb_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, IMG_WIDTH, IMG_HEIGHT,
                 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());


    // Render the scene for each eye
    for(int index = 0; index < ovrEye_Count; ++index)
    {
        ovrEyeType curr_eye = hmd->EyeRenderOrder[index];

        // Bind framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, frame_buffers[curr_eye]);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Get a projection matrix from LibOVR.
        Matrix4f projection = ovrMatrix4f_Projection(eyeRenderDesc[curr_eye].Fov, .01, 100, false);

        // Calculate left handed up vector and forward vector for eye
        Matrix4f view = Matrix4f(eyePoses[curr_eye].Orientation);
        OVR::Vector3f up          = view.Transform(OVR::Vector3f(0, -1, 0));
        OVR::Vector3f forward     = view.Transform(OVR::Vector3f(0, 0, -1));

        // Get view matrix from LibOVR.
        // Orientation + position in left handed system.
        view = OVR::Matrix4f::LookAtLH(eyePoses[curr_eye].Position,
                                       OVR::Vector3f(eyePoses[curr_eye].Position) + forward,
                                       up);

        // Set projection matrix.
        glMatrixMode(GL_PROJECTION);
        glLoadMatrixf(&projection.Transposed().M[0][0]);

        // Set view matrix.
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(&view.Transposed().M[0][0]); // Camera position/orientation
        // .5 meters from position tracking camera is a good distance to call center
        glTranslatef(0, 0, .5);           // Move World
                                          // Rotate World

        // Draw cube (virtual room)
        glPushMatrix();
            // Position tracking camera is 1.2m high (0 y coordinate),
            // virtual floor is 1.5m below 0 y, move up by difference.
            glTranslatef(0, .3, 0);       // Move "room"
                                          // Rotate "room"

            glColor4f(0.5, 0.5, 0.5, 1.0);
            glutSolidCube(3.8);
        glPopMatrix();

        // Draw Kinect geometry
        glPushMatrix();
            // Transform Kinect geometry
            // Negate Z because image is behind
            // Kinect is positioned .5 meters above position tracking camera,
            // reduced by .1 meters due to angle of camera.
            glTranslatef(-.5, .4, 1.6);    // Move geometry
                                           // Rotate geometry
            glScalef( 1, 1, -1);

            glColor4f(1, 0, 0, 1);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);

            if (device->getDisplayMode() == MyFreenectDevice::POINTS)
            {
                // Draw point cloud
                glDrawArrays( GL_POINTS, 0, IMG_WIDTH * IMG_HEIGHT/4 );
            }
            else if (device->getDisplayMode() == MyFreenectDevice::TRIANGLES)
            {
                // Draw triangle strip
                glUseProgram(hide_invalid_vertices);
                glDrawElements( GL_TRIANGLE_STRIP, indices.size(), GL_UNSIGNED_INT, &indices.front() );
                glUseProgram(0);
            }

            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glPopMatrix();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Tell LibOVR to display the rendered scene.
    ovrHmd_EndFrame(hmd, eyePoses, &eyeTextures[0].Texture);
}


// This is executed when there is no input.
void idleFunc()
{
    // Written by Glenn G. Chappell
    static int error_count = 0;
    if (GLenum err = glGetError())
    {
        ++error_count;
        std::cerr << "OpenGL ERROR " << error_count << ": "
             << gluErrorString(err) << endl;
    }
    
    glutPostRedisplay();
}


// This handles keyboard keypresses.
void keyPressed(unsigned char key, int x, int y)
{
    switch (key)
    {
        case ESC: // Shutdown program
            glutDestroyWindow(window);

            // Clean up hmd and oculus VR library.
            ovrHmd_Destroy(hmd);
            ovr_Shutdown();

            cout << endl << "Finished" << endl << endl;
            break;
        case 'v': // Toggle display mode between point cloud and triangle strip.
            cout << endl << endl << " Changing display mode to: ";
            device->toggleDisplayMode();
            if(device->getDisplayMode() == MyFreenectDevice::POINTS)
                cout << "POINTS" << endl;
            else if(device->getDisplayMode() == MyFreenectDevice::TRIANGLES)
                cout << "TRIANGLES" << endl;
            break;

        // Change verticle tilt angle of Kinect.
        case'w':
            freenect_angle++;
            if (freenect_angle > 30)
            {
                freenect_angle = 30;
            }
            break;
        case 's':
        case 'd':
            freenect_angle = 0;
            break;
        case 'x':
            freenect_angle--;
            if (freenect_angle < -30)
            {
                freenect_angle = -30;
            }
            break;
        case 'e':
            freenect_angle = 10;
            break;
        case 'c':
            freenect_angle = -10;
            break;
        default: ;
    }

    device->setTiltDegrees(freenect_angle);
}


// Tracks the current mouse position when
// the mouse button is held and the mouse is moved.
void clickAndDrag(int x, int y)
{
    saved_x = x;
    saved_y = y;
}


// Tracks the current mouse position when mouse is moved.
void mouseMove(int x, int y)
{
    saved_x = x;
    saved_y = y;
}


// Generate texture coordinate array for triangle strip assuming every pixel is a vertex
void generateTextureCoords()
{
    const float fovCorrection = .92185;
    const float offset = (1 - fovCorrection) / 2;
    for( unsigned yy = 0; yy < IMG_HEIGHT; yy+=2 ) {
        for( unsigned xx = 0; xx < IMG_WIDTH; xx+=2 ) {

            texCoords.push_back(float(xx) / IMG_WIDTH * fovCorrection + offset);
            texCoords.push_back(float(yy) / IMG_HEIGHT * fovCorrection + 1.5 * offset);
        }
    }

    glTexCoordPointer(2, GL_FLOAT, 0, &texCoords.front());
}


// Initialize rendering variables, and set up shaders.
void InitGL(unsigned int tex_w, unsigned int tex_h)
{
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(1.0);

    // Compile shaders into a program.
    glewInit();
    string vShader = "shaders/invalids_v.glsl";
    string gShader = "shaders/normals_g.glsl";
    string fShader = "shaders/invalids_f.glsl";    
    hide_invalid_vertices = makeShaderProgramFromFiles(vShader, gShader, fShader);

    // Create a texture for coloring Kinect geometry.
    glGenTextures(1, &gl_rgb_tex);
    glBindTexture(GL_TEXTURE_2D, gl_rgb_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    generateTextureCoords();

    // Create textures for each eye, and framebuffers for drawing to the textures.
    glGenTextures(2, eye_tex);
    glGenFramebuffers(2, frame_buffers);
    GLuint render_buffers[2];
    glGenRenderbuffers(2, render_buffers);

    // For position tracking.
    ovrHmd_ConfigureTracking(hmd, ovrTrackingCap_Orientation |
                                  ovrTrackingCap_MagYawCorrection |
                                  ovrTrackingCap_Position, 0);
    // Configure OVR rendering.
    ovrGLConfig apiConfig;
    apiConfig.OGL.Header.API = ovrRenderAPI_OpenGL;
    apiConfig.OGL.Header.BackBufferSize = OVR::Sizei(hmd->Resolution.w,
                                                     hmd->Resolution.h);
    apiConfig.OGL.Header.Multisample = 1;
    apiConfig.OGL.Disp = NULL;

    ovrHmd_ConfigureRendering(hmd,
                              &apiConfig.Config,
                              hmd->DistortionCaps,
                              hmd->DefaultEyeFov,
                              eyeRenderDesc);

    // Set up render textures for each eye, and pass information to LibOVR.
    for(int eye = 0; eye < 2; ++eye)
    {
        // Make empty texture with correct size.
        glBindTexture(GL_TEXTURE_2D, eye_tex[eye]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_w, tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        // Attach texture to render buffer.
        glBindFramebuffer(GL_FRAMEBUFFER, frame_buffers[eye]);
        glBindRenderbuffer(GL_RENDERBUFFER, render_buffers[eye]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, tex_w, tex_h);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, eye_tex[eye], 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, render_buffers[eye]);

        // Give texture handles to LibOVR.
        eyeTextures[eye].OGL.Header.API = ovrRenderAPI_OpenGL;
        eyeTextures[eye].OGL.Header.TextureSize = OVR::Sizei(tex_w, tex_h);
        eyeTextures[eye].OGL.Header.RenderViewport = OVR::Recti(0, 0, tex_w, tex_h);
        eyeTextures[eye].OGL.TexId = eye_tex[eye];
    }

    // Bind default texture and frame buffers for safety.
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Enable simple lighting
    glEnable(GL_LIGHTING);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_LIGHT0);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);

    glEnable(GL_DEPTH_TEST);
}


// Generate index array for triangle strip assuming every pixel is a vertex
void makeIndexArray()
{
    unsigned height = IMG_HEIGHT/2; // Only using every other pixel in depth image.
    unsigned width = IMG_WIDTH/2;   // Only using every other pixel in depth image.

    for( unsigned yy = 0; yy < height-1; ++yy ) {
        for( unsigned xx = 0; xx < width-1; ++xx ) {

            unsigned x;
            if( yy%2 == 1 ) x = width - xx - 1; // Odd rows go backwards
            else x = xx;                        // Even rows
            
            // Push back vertical pairs of vertices.
            indices.push_back(yy * width + x);
            indices.push_back((yy+1) * width + x);
        }
        
        // Only add one at the end of each row,
        // the start of the next row will add the other.
        if( yy%2 == 0 )
            indices.push_back((yy * width) + (width - 1));
        else
            indices.push_back(yy * width);
    }
}


// Handles OpenGL in separate thread.
void *gl_threadfunc(void *arg)
{
    cout << "GL thread" << endl;

    // Initialize Oculus VR library.
    if( !ovr_Initialize() )
    {
        cout<< "Failed to Initialize OVR" << endl;
        exit(1);
    }

    cout << "libOVR initialized." << endl;

    // Create an HMD object with data about the head mounted display.
    hmd = ovrHmd_Create(0);
    if( !hmd )
    {
        cout << OVR::Service::NetClient::GetInstance()->Hmd_GetLastError(0) << endl;
        ovr_Shutdown();
        exit(1);
    }

    cout << "HMD created." << endl;

    // If the hmd capabilities does not include extended desktop mode.
    if( !(hmd->HmdCaps & ovrHmdCap_ExtendDesktop) )
    {
        cout << "Not in extended desktop mode." << endl;
        ovrHmd_Destroy(hmd);
        ovr_Shutdown();
        exit(0);
    }

    makeIndexArray();

    // Initialize glut and create window with oculus HMD display size
    glutInit(&g_argc, g_argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_ALPHA | GLUT_DEPTH);
    glutInitWindowSize(hmd->Resolution.w, hmd->Resolution.h);
    glutInitWindowPosition(0, 0);

    window = glutCreateWindow("Virtualized Reality with Oculus and Kinect");

    // Register glut callback functions
    glutDisplayFunc(&DrawGLScene);
    glutIdleFunc(&idleFunc);
    glutKeyboardFunc(&keyPressed);
    glutMotionFunc(&clickAndDrag);
    glutPassiveMotionFunc(&mouseMove);

    texture_w = hmd->Resolution.w/2;
    texture_h = hmd->Resolution.h;

    InitGL(texture_w, texture_h);

    glutMainLoop();

    return NULL;
}


int main(int argc, char **argv)
{
    device = &freenect.createDevice<MyFreenectDevice>(0);
    if( device )
    {
        // Start Kinect processing.
        device->startVideo();
        device->startDepth();

        // Start Rendering in separate thread.
        gl_threadfunc(NULL);
    }
    else
    {
        cerr << "Failed to create Freenect Device." << endl;
    }

    return 0;
}
