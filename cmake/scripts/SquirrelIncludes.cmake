cmake_minimum_required(VERSION 3.5)

if(NOT INCLUDES_SOURCE_FILE)
    message(FATAL_ERROR "Script needs INCLUDES_SOURCE_FILE defined")
endif()
if(NOT INCLUDES_BINARY_FILE)
    message(FATAL_ERROR "Script needs INCLUDES_BINARY_FILE defined")
endif()
if(NOT APILC)
    message(FATAL_ERROR "Script needs APILC defined")
endif()
if(NOT APIUC)
    message(FATAL_ERROR "Script needs APIUC defined")
endif()

set(ARGC 1)
set(ARG_READ NO)

# For MSVC CMake runs this script from a batch file using || to detect errors,
# depending on source path it may quote args, and cause cmd to not understand ||
# and pass it as argument to ourself.
# Read all the arguments given to CMake; we are looking for -- and everything
# that follows, until ||. Those are our api files.
while(ARGC LESS CMAKE_ARGC)
    set(ARG ${CMAKE_ARGV${ARGC}})

    if(ARG STREQUAL "||")
        set(ARG_READ NO)
    endif()

    if(ARG_READ)
        list(APPEND SCRIPT_API_BINARY_FILES "${ARG}")
    endif()

    if(ARG STREQUAL "--")
        set(ARG_READ YES)
    endif()

    math(EXPR ARGC "${ARGC} + 1")
endwhile()

foreach(FILE IN LISTS SCRIPT_API_BINARY_FILES)
    file(STRINGS "${FILE}" LINES REGEX "^void SQ${APIUC}.*_Register\\(Squirrel \\*engine\\)$")
    if(LINES)
        string(REGEX REPLACE ".*api/${APILC}/(.*)" "#include \"\\1\"" FILE "${FILE}")
        list(APPEND SQUIRREL_INCLUDES "${FILE}")
        foreach(LINE IN LISTS LINES)
            if("${LINE}" MATCHES "SQ${APIUC}(List|Controller)_Register")
                continue()
            endif()
            string(REGEX REPLACE "^.*void " "	" LINE "${LINE}")
            string(REGEX REPLACE "Squirrel \\*" "" LINE "${LINE}")
            list(APPEND SQUIRREL_REGISTER "${LINE}")
        endforeach()
    endif()
endforeach()

list(SORT SQUIRREL_INCLUDES)
string(REPLACE ";" "\n" SQUIRREL_INCLUDES "${SQUIRREL_INCLUDES}")

string(REGEX REPLACE "_Register" "0000Register" SQUIRREL_REGISTER "${SQUIRREL_REGISTER}")
list(SORT SQUIRREL_REGISTER)
string(REGEX REPLACE "0000Register" "_Register" SQUIRREL_REGISTER "${SQUIRREL_REGISTER}")
string(REPLACE ";" ";\n" SQUIRREL_REGISTER "${SQUIRREL_REGISTER}")
set(SQUIRREL_REGISTER "	SQ${APIUC}List_Register(engine);\n${SQUIRREL_REGISTER};")

configure_file(${INCLUDES_SOURCE_FILE} ${INCLUDES_BINARY_FILE})
