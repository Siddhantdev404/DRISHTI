// ios/UptimeModule.swift
import Foundation

@objc(UptimeModule)
class UptimeModule: NSObject {

  // mach_absolute_time() is the raw hardware tick counter.
  // mach_timebase_info converts ticks to nanoseconds.
  // Divide by 1,000,000 for milliseconds. Cannot be set by the user.
  @objc func getUptimeMs() -> Double {
    var info = mach_timebase_info_data_t()
    mach_timebase_info(&info)
    let ticks = mach_absolute_time()
    let nanos = Double(ticks) * Double(info.numer) / Double(info.denom)
    return nanos / 1_000_000.0
  }
}
