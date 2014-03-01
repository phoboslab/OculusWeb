# OculusWeb

A fast and lean HTTP/WebSocket Oculus Rift Tracking Server for Windows and Mac. Mostly C code, compiled with CPP for libOVR. This should also compile on Linux - Pull Requests for a makefile are welcome.

Uses the excellent [libwebsockets](http://libwebsockets.org/) library.

OculusWeb is published under the MIT Open Source License.


## Endpoints

When started, the server runs on port 9006. If the Oculus Rift is connected, you should see the following output in the console window:

```
Awaiting WebSocket connections on ws://localhost:9006
Device Info at http://localhost:9006/device
Current Orientation at http://localhost:9006/orientation
```

`ws://localhost:9006` continuously sends out the current orientation of the rift as a quaternion `[w, x, y, z]`. The same data is available for polling at `http://localhost:9006/orientation`

`http://localhost:9006/device` return metrics about the currently connected device. E.g.:

```
{
	fov: 125.870987,
	hScreenSize: 0.149760,
	vScreenSize: 0.093600,
	vScreenCenter: 0.046800,
	eyeToScreenDistance: 0.041000,
	lensSeparationDistance: 0.063500,
	interpupillaryDistance: 0.063100,
	hResolution: 1280,
	vResolution: 800,
	distortionK: [1.000000, 0.220000, 0.240000, 0.000000],
	chromaAbCorrection: [0.996000, -0.004000, 1.014000, 0.000000]
}
```

## Usage

When the Tracking Server is running, you can open a WebSocket connection in you browser and receive tracking data:

```javascript
var ws = new WebSocket('ws://localhost:9006');
ws.onmessage = function(ev) {
	var quat = JSON.parse(ev.data);
	// do something with the quaternion here...
};
```

Polling with a blocking XMLHttpRequest for every frame also works surprisingly well:

```javascript
var req = new XMLHttpRequest;
req.open('GET', 'http://localhost:9006/orientation', false); // false = blocking
req.send();
var quat = JSON.parse(req.responseText);
```

You can change the default WebSocket send interval (default 2ms) and OVR's predicition value (default 40ms) through an HTTP POST:

```javascript
var req = new XMLHttpRequest();
req.open('POST', 'http://localhost:9006/set');
req.send('interval=10&prediction=20');
```
