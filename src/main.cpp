#include "libwebsocket/libwebsockets.h"
#include "OVR.h"

#define OW_RX_BUFFER_SIZE (100*1024) // 100kb
static unsigned sendInterval = 2;

static char _send_buffer_with_padding[LWS_SEND_BUFFER_PRE_PADDING + OW_RX_BUFFER_SIZE + LWS_SEND_BUFFER_POST_PADDING];
static char *send_buffer = (char *)&_send_buffer_with_padding[LWS_SEND_BUFFER_PRE_PADDING];

static OVR::DeviceManager *manager;
static OVR::HMDDevice *hmd;
static OVR::SensorDevice *sensor;
static OVR::SensorFusion *fusion;
static OVR::HMDInfo hmdinfo;
static OVR::Util::Render::StereoConfig stereoConfig;

static const char *httpHeader =
	"HTTP/1.0 %u %s\x0d\x0a"
	"Server: libwebsockets\x0d\x0a"
	"Access-Control-Allow-Origin: *\x0d\x0a"
	"Mime-Type: text/plain\x0d\x0a\x0d\x0a";

#ifdef _WIN32
	#include <windows.h>
	void ow_sleep(unsigned milliseconds) { Sleep(milliseconds); }
#else
	#include <unistd.h>
	void ow_sleep(unsigned milliseconds) { usleep(milliseconds * 1000); }
#endif

static int callback_websockets(
	struct libwebsocket_context *context,
	struct libwebsocket *wsi,
	enum libwebsocket_callback_reasons reason,
	void *user, void *in, size_t len
) {
	if( reason != LWS_CALLBACK_SERVER_WRITEABLE ) { return 0; }
	
	OVR::Quatf q = fusion->GetPredictedOrientation();
	sprintf(send_buffer, "[%f,%f,%f,%f]", q.w, q.x, q.y, q.z);
	libwebsocket_write(wsi, (unsigned char *)send_buffer, strlen(send_buffer), LWS_WRITE_TEXT);
	return 0;
}

static int callback_http(
	struct libwebsocket_context *context,
	struct libwebsocket *wsi,
	enum libwebsocket_callback_reasons reason, void *user,
	void *in, size_t len
) {
	// HTTP body for post request - only allowed for /set URL
	if( reason == LWS_CALLBACK_HTTP_BODY ) {
		char *body = (char *)in;
		do {
			char name[32];
			int value;

			if( sscanf(body, "%31[^=&]=%d", name, &value) == 2 ) {
				if( strcmp(name, "interval") == 0 && value ) {
					printf("setting send interval to %dms", value);
					sendInterval = value;
				}
				else if( strcmp(name, "prediction") == 0 ) {
					printf("setting prediction to %dms", value);
					fusion->SetPrediction((float)value/1000, !!value);
				}
			}
		} while( (body = strchr(body, '&')) && ++body );
		
		return -1;
	}
	
	else if( reason == LWS_CALLBACK_HTTP ) {
		char *request = (char *)in;
		if( strcmp(request, "/orientation") == 0 && hmd ) {
			OVR::Quatf q = fusion->GetPredictedOrientation();
			sprintf(send_buffer, "%s[%f,%f,%f,%f]", httpHeader, q.w, q.x, q.y, q.z);
			libwebsocket_write_http(wsi, (unsigned char*)send_buffer, strlen(send_buffer));
		}
		else if( strcmp(request, "/device") == 0 && hmd ) {
			hmd->GetDeviceInfo(&hmdinfo);
			sprintf(send_buffer, "%s"
				"{"
					"fov: %f,"
					"hScreenSize: %f,"
					"vScreenSize: %f,"
					"vScreenCenter: %f,"
					"eyeToScreenDistance: %f,"
					"lensSeparationDistance: %f,"
					"interpupillaryDistance: %f,"
					"hResolution: %d,"
					"vResolution: %d,"
					"distortionK: [%f, %f, %f, %f],"
					"chromaAbCorrection: [%f, %f, %f, %f]"
				"}",
				httpHeader,
				stereoConfig.GetYFOVDegrees(),
				hmdinfo.HScreenSize,
				hmdinfo.VScreenSize,
				hmdinfo.VScreenCenter,
				hmdinfo.EyeToScreenDistance,
				hmdinfo.LensSeparationDistance,
				hmdinfo.InterpupillaryDistance,
				hmdinfo.HResolution,
				hmdinfo.VResolution,
				hmdinfo.DistortionK[0], hmdinfo.DistortionK[1], hmdinfo.DistortionK[2], hmdinfo.DistortionK[3],
				hmdinfo.ChromaAbCorrection[0], hmdinfo.ChromaAbCorrection[1], hmdinfo.ChromaAbCorrection[2], hmdinfo.ChromaAbCorrection[3]
			);
			libwebsocket_write_http(wsi, (unsigned char*)send_buffer, strlen(send_buffer));
		}
		else if( strstr(request, "/set") == request ) {
			if( lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI) ) {
				// Post request - let it continue
				return 0;
			}
		}
		else {
			libwebsockets_return_http_status(context, wsi, HTTP_STATUS_NOT_FOUND, NULL);
		}
		return -1;
	}

	else {
		return 0;
	}
}



int main(int argc, const char * argv[]) {
	// Setup OVR
	OVR::System::Init(OVR::Log::ConfigureDefaultLog(OVR::LogMask_All));
	manager = OVR::DeviceManager::Create();
	hmd = manager->EnumerateDevices<OVR::HMDDevice>().CreateDevice();
	
	if( !hmd ) {
		printf("ERROR: Oculus Rift not connected\n");
		return 1;
	}

	sensor = hmd->GetSensor();
	if( !sensor ) {
		printf("ERROR: Getting Sensor Failed\n");
		delete hmd;
		return 1;
	}

	fusion = new OVR::SensorFusion();
	fusion->AttachToSensor(sensor);
	fusion->SetYawCorrectionEnabled(true);
	fusion->SetPrediction(0.04f, true);
	
	
	// Setup the WebSocket server
	struct libwebsocket_protocols protocols[] = {
		{ "http-only", callback_http, 0 },
		{ NULL, callback_websockets, sizeof(int), OW_RX_BUFFER_SIZE },
		{ NULL, NULL, 0 /* End of list */ }
	};

	struct lws_context_creation_info wsinfo = {0};
	wsinfo.port = 9006;
	wsinfo.gid = -1;
	wsinfo.uid = -1;
	wsinfo.protocols = protocols;
	libwebsocket_context *ws = libwebsocket_create_context(&wsinfo);
	
	if( !ws ) {
		printf("ERROR: Couldn't create WebSocket Server at ws://localhost:%d (port already in use?)\n", wsinfo.port);
		return 1;
	}
	
	
	// Run
	printf("Awaiting WebSocket connections on ws://localhost:%d\n", wsinfo.port);
	printf("Device Info at http://localhost:%d/device\n", wsinfo.port);
	printf("Current Orientation at http://localhost:%d/orientation\n", wsinfo.port);
	while( true ) {
		libwebsocket_callback_on_writable_all_protocol(&(protocols[1]));
		libwebsocket_service(ws, 0);
		ow_sleep(sendInterval);
	}
	
	
	// Cleanup
	libwebsocket_context_destroy(ws);
	
	manager->Release();
	hmd->Release();
	sensor->Release();
	delete fusion;
	
	return 0;
}
