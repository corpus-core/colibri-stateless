add_library(eth_precompiles STATIC 
  precompiles_basic.c

  
)
set (DEPS util crypto)
target_include_directories(eth_precompiles INTERFACE .)

if (INTX)
  set (DEPS ${DEPS} intx_wrapper)
endif()

target_link_libraries(eth_precompiles PUBLIC ${DEPS})
