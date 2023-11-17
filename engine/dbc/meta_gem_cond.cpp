#include "config.hpp"

#include "meta_gem_cond.hpp"
#include "generated/meta_gem_cond.inc"

#include <array>
#if SC_USE_PTR == 1
#include "generated/meta_gem_cond_ptr.inc"
#endif

util::span<const meta_gem_cond_t> meta_gem_cond_t::data( bool ptr )
{
  return SC_DBC_GET_DATA( __meta_gem_cond_data, __ptr_meta_gem_cond_data, ptr );
}