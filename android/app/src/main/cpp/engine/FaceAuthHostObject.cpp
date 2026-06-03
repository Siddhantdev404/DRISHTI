#include "FaceAuthHostObject.h"

namespace drishti {

facebook::jsi::Value FaceAuthHostObject::get(facebook::jsi::Runtime& rt, const facebook::jsi::PropNameID& name) {
    auto propName = name.utf8(rt);

    if (propName == "call") {
        return facebook::jsi::Function::createFromHostFunction(
            rt, 
            name, 
            1, 
            [](facebook::jsi::Runtime& rt, const facebook::jsi::Value& thisVal, const facebook::jsi::Value* args, size_t count) -> facebook::jsi::Value {
                
                auto resultObj = facebook::jsi::Object(rt);
                
                // Hardcoded stub values per spec
                resultObj.setProperty(rt, "livenessState", "LIVENESS_PASS");
                resultObj.setProperty(rt, "matchScore", 0.92);
                resultObj.setProperty(rt, "matchedId", "stub-uuid-1234");
                
                // Fill out the rest of the FaceAuthResult schema so TS side doesn't break
                resultObj.setProperty(rt, "activeChallenge", "NONE");
                resultObj.setProperty(rt, "errorCode", "");
                resultObj.setProperty(rt, "embeddingReady", false);
                resultObj.setProperty(rt, "inferenceMs", 12.0);
                resultObj.setProperty(rt, "ear", 0.0);
                resultObj.setProperty(rt, "mar", 0.0);
                resultObj.setProperty(rt, "yaw", 0.0);
                resultObj.setProperty(rt, "pitch", 0.0);
                resultObj.setProperty(rt, "tempVariance", 0.0);
                resultObj.setProperty(rt, "frameCount", 1);
                resultObj.setProperty(rt, "nativeFps", 30);
                resultObj.setProperty(rt, "nativeHeapKB", 1024);

                return resultObj;
            }
        );
    }
    
    return facebook::jsi::Value::undefined();
}

} // namespace drishti
