if(TARGET thinkboxlibrary)
  set(thinkboxlibrary_FOUND TRUE)
  get_target_property(thinkboxlibrary_INCLUDE_DIRS thinkboxlibrary INTERFACE_INCLUDE_DIRECTORIES)
  
  if(NOT TARGET thinkboxlibrary::thinkboxlibrary)
    add_library(thinkboxlibrary::thinkboxlibrary ALIAS thinkboxlibrary)
  endif()
endif()
