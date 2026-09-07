find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PC_FFTW QUIET fftw3f)
endif()
find_path(FFTW3f_INCLUDE_DIR fftw3.h HINTS ${PC_FFTW_INCLUDE_DIRS})
find_library(FFTW3f_LIBRARY NAMES fftw3f HINTS ${PC_FFTW_LIBRARY_DIRS})
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFTW3f DEFAULT_MSG FFTW3f_LIBRARY FFTW3f_INCLUDE_DIR)
if(FFTW3f_FOUND AND NOT TARGET FFTW3::fftw3f)
  add_library(FFTW3::fftw3f UNKNOWN IMPORTED)
  set_target_properties(FFTW3::fftw3f PROPERTIES
    IMPORTED_LOCATION "${FFTW3f_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FFTW3f_INCLUDE_DIR}")
endif()
