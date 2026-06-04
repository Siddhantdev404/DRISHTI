package com.drishti

import com.mrousavy.camera.frameprocessor.Frame
import com.mrousavy.camera.frameprocessor.FrameProcessorPlugin
import com.mrousavy.camera.frameprocessor.VisionCameraProxy

class FaceAuthFrameProcessorPlugin(proxy: VisionCameraProxy, options: Map<String, Any>?) : FrameProcessorPlugin() {
    private var frameCount = 0

    override fun callback(frame: Frame, params: Map<String, Any>?): Any? {
        val image = frame.image
        if (image == null) {
            android.util.Log.e("FaceAuthFrameProcessor", "Frame image is null!")
            return null
        }

        // We only care about the Y plane (luminance) for FaceMesh
        val yPlane = image.planes[0]
        val buffer = yPlane.buffer
        val width = image.width
        val height = image.height
        val stride = yPlane.rowStride

        val rotation = (params?.get("rotation") as? Double)?.toInt() ?: 90

        // Directly invoke the C++ native method, bypassing JS entirely for frame processing
        val accepted = FaceAuthModule.nativeProcessFrame(buffer, width, height, stride, rotation)
        frameCount++
        if (frameCount % 30 == 0) {
            android.util.Log.i(
                "FaceAuthFrameProcessor",
                "[DRISHTI_RUNTIME] callback frame=$frameCount accepted=$accepted size=${width}x$height stride=$stride rotation=$rotation"
            )
        }

        return null
    }
}
