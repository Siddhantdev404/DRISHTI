package com.drishti

import com.mrousavy.camera.frameprocessor.Frame
import com.mrousavy.camera.frameprocessor.FrameProcessorPlugin
import com.mrousavy.camera.frameprocessor.VisionCameraProxy

class FaceAuthFrameProcessorPlugin(proxy: VisionCameraProxy, options: Map<String, Any>?) : FrameProcessorPlugin() {

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

        // Directly invoke the C++ native method, bypassing JS entirely for frame processing
        FaceAuthModule.nativeProcessFrame(buffer, width, height, stride)

        return null
    }
}
