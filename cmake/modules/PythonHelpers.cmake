function(pyinstall)
  set(options)
  set(oneValueArgs BASEPATH DESTINATION)
  set(multiValueArgs FILES)
  cmake_parse_arguments(pyinstall "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (NOT pyinstall_BASEPATH)
    message(SEND_ERROR "Error: BASEPATH not specified")
  endif()

  if (NOT pyinstall_DESTINATION)
    message(SEND_ERROR "Error: DESTINATION not specified")
  endif()

  if (NOT pyinstall_FILES)
    message(SEND_ERROR "Error: FILES not specified")
  endif()

  set(_rel_path_list)
  foreach(PPY ${pyinstall_FILES})
    file(RELATIVE_PATH _rel ${pyinstall_BASEPATH} ${PPY})
    get_filename_component(_rel_path ${_rel} PATH)
    install(FILES ${PPY} DESTINATION ${pyinstall_DESTINATION}/${_rel_path})
    list(APPEND _rel_path_list ${_rel_path})
  endforeach()

  # In order for python to see the installed py files as a valid module 
  # there needs to be an __init__.py in each dir that the pys are installed
  # into. So we build a list of those dirs here.
  set(_init_list)
  list(REMOVE_DUPLICATES _rel_path_list)
  foreach(_rel_path ${_rel_path_list})
    # Split the path into a list by folder
    string(REGEX REPLACE "/" ";" _path_split ${_rel_path})

    set(_running_split)
    foreach(_part ${_path_split})
      # One folder at a time piece it together
      set(_running_split ${_running_split} ${_part})
      string(REGEX REPLACE ";" "/" _running_path "${_running_split}")

      # Add to _init_list if it's not there already
      list(FIND _init_list ${_running_path} _contains_already)
      if (${_contains_already} EQUAL -1)
        list(APPEND _init_list ${_running_path})
      endif()
    endforeach()
  endforeach()

  # Now, for each init path, add an __init__.py
  foreach(_rel_path ${_init_list})
    install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E touch ${pyinstall_DESTINATION}/${_rel_path}/__init__.py)")
  endforeach()
endfunction()

function(pyinstall_bin)
  set(options)
  set(oneValueArgs BASEPATH DESTINATION)
  set(multiValueArgs FILES)
  cmake_parse_arguments(pyinstall_bin "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (NOT pyinstall_bin_BASEPATH)
    message(SEND_ERROR "Error: BASEPATH not specified")
  endif()

  if (NOT pyinstall_bin_DESTINATION)
    message(SEND_ERROR "Error: DESTINATION not specified")
  endif()

  if (NOT pyinstall_bin_FILES)
    message(SEND_ERROR "Error: FILES not specified")
  endif()


  foreach(pyfile ${pyinstall_bin_FILES})
    string(REPLACE ".py" "" binfile ${pyfile})

    set(CONFIG_PY_DIR ${pyinstall_bin_BASEPATH})
    configure_file(${pyfile} ${binfile} @ONLY)

    set(CONFIG_PY_DIR ${pyinstall_bin_DESTINATION})
    configure_file(${pyfile} .pyinstall/${binfile} @ONLY)
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/.pyinstall/${binfile} DESTINATION bin)
  endforeach()

endfunction()
