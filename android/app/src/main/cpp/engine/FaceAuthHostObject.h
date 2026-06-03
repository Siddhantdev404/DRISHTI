#pragma once

#include <jsi/jsi.h>
#include "../shared/FaceAuthResult.h"

namespace drishti {

class FaceAuthHostObject : public facebook::jsi::HostObject {
public:
    FaceAuthHostObject() = default;
    virtual ~FaceAuthHostObject() = default;

    facebook::jsi::Value get(facebook::jsi::Runtime& rt, const facebook::jsi::PropNameID& name) override;
};

} // namespace drishti
