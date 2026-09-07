find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PC_RB QUIET rubberband)
endif()
find_path(RubberBand_INCLUDE_DIR rubberband/RubberBandStretcher.h HINTS ${PC_RB_INCLUDE_DIRS})
find_library(RubberBand_LIBRARY NAMES rubberband HINTS ${PC_RB_LIBRARY_DIRS})
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RubberBand DEFAULT_MSG RubberBand_LIBRARY RubberBand_INCLUDE_DIR)
if(RubberBand_FOUND AND NOT TARGET RubberBand::RubberBand)
  add_library(RubberBand::RubberBand UNKNOWN IMPORTED)
  set_target_properties(RubberBand::RubberBand PROPERTIES
    IMPORTED_LOCATION "${RubberBand_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${RubberBand_INCLUDE_DIR}")
endif()
