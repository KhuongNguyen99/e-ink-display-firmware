idf_component_register(SRCS "main.cpp" "panel.cpp" "hkdf_fallback.c" "pm_build_check.cpp"
                       INCLUDE_DIRS "." REQUIRES arduino json bt esp_pm)
if(NOT COUNTWELL_PANEL MATCHES "^(ssd1680|jd79661)$")
    message(FATAL_ERROR "COUNTWELL_PANEL must be ssd1680 or jd79661")
endif()
string(TOUPPER "${COUNTWELL_PANEL}" panel_define)
target_compile_definitions(${COMPONENT_LIB} PRIVATE "COUNTWELL_${panel_define}")
