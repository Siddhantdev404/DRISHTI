if(NOT TARGET react-native-worklets-core::rnworklets)
add_library(react-native-worklets-core::rnworklets INTERFACE IMPORTED)
set_target_properties(react-native-worklets-core::rnworklets PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "D:/DRISHTI/node_modules/react-native-worklets-core/android/build/headers/rnworklets"
    INTERFACE_LINK_LIBRARIES ""
)
endif()

