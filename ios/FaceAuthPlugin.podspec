require "json"

package = JSON.parse(File.read(File.join(__dir__, "../package.json")))

Pod::Spec.new do |s|
  s.name         = "FaceAuthPlugin"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => "13.0" }
  s.source       = { :git => "https://github.com/Drishti/FaceAuthPlugin.git", :tag => "#{s.version}" }

  s.source_files = "ios/**/*.{h,m,mm,swift}", "android/app/src/main/cpp/**/*.{h,cpp}"
  
  # Exclude iOS Simulator architecture due to TFLite C incompatibility
  s.pod_target_xcconfig = { 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64' }
  
  # Vendor TFLite C framework
  s.vendored_frameworks = 'ios/TensorFlowLiteC.xcframework'

  s.dependency "React-Core"
  s.dependency "VisionCamera"
end
