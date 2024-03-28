#include "config.hpp"

#include "rand_suffix_data.hpp"

#include <array>

#include "generated/rand_suffix_data.inc"
#if SC_USE_PTR == 1
#include "generated/rand_suffix_data_ptr.inc"
#endif

util::span<const random_suffix_data_t> random_suffix_data_t::data( bool ptr )
{
  return SC_DBC_GET_DATA( __rand_suffix_data, __ptr_rand_suffix_data, ptr );
}
