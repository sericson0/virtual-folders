# Copy the built DLL into the VirtualDJ plugin folder, ignoring any failure
# (VDJ not installed, or the DLL currently locked because the plugin is open).
# execute_process without COMMAND_ERROR_IS_FATAL never aborts the build.
file(MAKE_DIRECTORY "${DEST_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC}" "${DEST_DIR}/TigerFolders.dll"
    RESULT_VARIABLE _r
)
if(_r EQUAL 0)
    message(STATUS "Installed TigerFolders.dll -> ${DEST_DIR}")
else()
    message(STATUS "Skipped install (VDJ absent or DLL locked): ${DEST_DIR}")
endif()
