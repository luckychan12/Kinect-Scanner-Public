#include "GLSetup.h"
#include "Main.h"

//OpenGL includes
#include "GLFW/glfw3.h"
#include <glm/glm.hpp> 

//Kinect SDK includes
#include <NuiApi.h>
#include <NuiImageCamera.h>
#include <NuiSensor.h>

// OpenGL Variables
long depthToRgbMap[width*height*2];

// GL buffers to store point data
GLuint vboId;
GLuint cboId;

// Kinect variables
HANDLE depthStream;
HANDLE rgbStream;
INuiSensor* sensor;

bool kinectSetup() {
    // Error if there is not only 1 sensor
    int numSensors = -1;
    if (NuiGetSensorCount(&numSensors) < 0 || numSensors < 1) return false;
    if (NuiCreateSensorByIndex(0, &sensor) < 0) return false;

    // Initialize sensor
    sensor->NuiInitialize(NUI_INITIALIZE_FLAG_USES_DEPTH | NUI_INITIALIZE_FLAG_USES_COLOR);

	//Camera type, Resolution, Img Stream flags, Frames to buffer, Event Handle, Stream 
    sensor->NuiImageStreamOpen(NUI_IMAGE_TYPE_DEPTH, NUI_IMAGE_RESOLUTION_640x480, 0, 2, NULL, &depthStream);	//Create depth camera stream

	sensor->NuiImageStreamOpen(NUI_IMAGE_TYPE_COLOR, NUI_IMAGE_RESOLUTION_640x480, 0, 2, NULL, &rgbStream); 	//create colour stream
    return sensor;
}

void storeDepthAtPoint(const USHORT*& curr, int width, int height, float*& fdest, long*& depth2rgb)
{
	// Get depth of pixel in millimeters
	USHORT depth = NuiDepthPixelToDepth(*curr++);
	// Store coordinates of the point corresponding to this pixel
	Vector4 pos = NuiTransformDepthImageToSkeleton(width, height, depth << 3, NUI_IMAGE_RESOLUTION_640x480);
	*fdest++ = pos.x / pos.w;
	*fdest++ = pos.y / pos.w;
	*fdest++ = pos.z / pos.w;
	// Store the index into the color array corresponding to this pixel
	int status = NuiImageGetColorPixelCoordinatesFromDepthPixelAtResolution(
		NUI_IMAGE_RESOLUTION_640x480, NUI_IMAGE_RESOLUTION_640x480, NULL,
		width, height, depth << 3, depth2rgb, depth2rgb + 1);
	depth2rgb += 2;
}

void getDepthData(GLubyte* dest) {
	float* fdest = (float*) dest;
	long* lDepthToRgbMap = (long*) depthToRgbMap;
    NUI_IMAGE_FRAME imageFrame;
    NUI_LOCKED_RECT LockedRect; // Prevents Kinect from overwriting buffer
    if (sensor->NuiImageStreamGetNextFrame(depthStream, 0, &imageFrame) < 0) return; // unable to get next frame
    INuiFrameTexture* texture = imageFrame.pFrameTexture;
    texture->LockRect(0, &LockedRect, NULL, 0);
    if (LockedRect.Pitch != 0) {
        const USHORT* curr = (const USHORT*) LockedRect.pBits;
		//Loop through and store for all points
        for (int j = 0; j < height; ++j) {
			for (int i = 0; i < width; ++i) {
				storeDepthAtPoint(curr, i, j, fdest, lDepthToRgbMap);
			}
		}
    }
    texture->UnlockRect(0);
    sensor->NuiImageStreamReleaseFrame(depthStream, &imageFrame);
}

void storeRGBDataAtPoint(long*& ldepthToRgbMap, float*& fdest, const BYTE* start)
{
	// Get colour
	long x = *ldepthToRgbMap++;
	long y = *ldepthToRgbMap++;
	// Ignore if out of bounds
	if (x < 0 || y < 0 || x > width || y > height) {
		for (int n = 0; n < 3; ++n) *(fdest++) = 0.0f;
	}
	else {
		const BYTE* curr = start + (x + width * y) * 4;
		for (int n = 0; n < 3; ++n) *(fdest++) = curr[2 - n] / 255.0f;
	}
}

void getRgbData(GLubyte* dest) {
	float* fdest = (float*) dest;
	long* lDepthToRgbMap = (long*) depthToRgbMap;
	NUI_IMAGE_FRAME imageFrame;
    NUI_LOCKED_RECT LockedRect; // Prevents Kinect from overwriting buffer
    if (sensor->NuiImageStreamGetNextFrame(rgbStream, 0, &imageFrame) < 0) return; // unable to get next frame
    INuiFrameTexture* texture = imageFrame.pFrameTexture;
    texture->LockRect(0, &LockedRect, NULL, 0);
    if (LockedRect.Pitch != 0) {
        const BYTE* start = (const BYTE*) LockedRect.pBits;
		//Loop through and store for all points
        for (int j = 0; j < height; ++j) {
			for (int i = 0; i < width; ++i) {
				storeRGBDataAtPoint(lDepthToRgbMap, fdest, start);
			}
		}
    }
    texture->UnlockRect(0);
    sensor->NuiImageStreamReleaseFrame(rgbStream, &imageFrame);
}

// Copy kinect data to GPU
void getKinectData() {
	GLubyte* ptr;
	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	ptr = (GLubyte*) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	if (ptr) {
		getDepthData(ptr);
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);
	glBindBuffer(GL_ARRAY_BUFFER, cboId);
	ptr = (GLubyte*) glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	if (ptr) {
		getRgbData(ptr);
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);
}

void rotateCamera() {
	static double count = 0;
	static double angle = 0.;
	static double radius = 4.;
	double x = radius*sin(angle);
	double z = radius*(1-cos(angle)) - radius/2;
	glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
	gluLookAt(x,0,z,0,0,radius/2,0,1,0);

	//Control amount of rotation
	angle = sin(count)/1.2;
	// Control Speed of rotation
	count += 0.08;
}

//
void drawKinectData() {
	getKinectData();
	rotateCamera();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glVertexPointer(3, GL_FLOAT, 0, NULL);

	glBindBuffer(GL_ARRAY_BUFFER, cboId);
	glColorPointer(3, GL_FLOAT, 0, NULL);

	glPointSize(2.f);
	glDrawArrays(GL_POINTS, 0, width*height);

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
}

void bufferSetup()
{
	const int dataSize = width * height * 3 * 4;
	glGenBuffers(1, &vboId);
	glBindBuffer(GL_ARRAY_BUFFER, vboId);
	glBufferData(GL_ARRAY_BUFFER, dataSize, 0, GL_DYNAMIC_DRAW);
	glGenBuffers(1, &cboId);
	glBindBuffer(GL_ARRAY_BUFFER, cboId);
	glBufferData(GL_ARRAY_BUFFER, dataSize, 0, GL_DYNAMIC_DRAW);
}

void cameraSetup()
{
	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(45, width / (GLdouble)height, 0.1, 1000);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(0, 0, 0, 0, 0, 1, 0, 1, 0);
}

int main(int argc, char* argv[]) {
	// Check if failed to initialise 
    if (!init(argc, argv)) return 1;
    if (!kinectSetup()) return 1;

    // OpenGL setup
    glClearColor(0,0,0,0);
    glClearDepth(1.0f);

	bufferSetup();

	cameraSetup();


    execute();
    return 0;
}


