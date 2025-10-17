#pragma once

// Disable the non-threadsafe debug code in boost::circular_buffer before 1.62
#define BOOST_CB_DISABLE_DEBUG 1

// Make sure it is also disabled when >= 1.62
#ifndef BOOST_CB_ENABLE_DEBUG
#define BOOST_CB_ENABLE_DEBUG 0
#endif

#if BOOST_CB_ENABLE_DEBUG
// https://github.com/boostorg/circular_buffer/pull/9
// https://svn.boost.org/trac10/ticket/6277
#error Building with BOOST_CB_ENABLE_DEBUG prevents accessing a boost::circular_buffer from more than one thread at once
#endif /* BOOST_CB_ENABLE_DEBUG */

#include <boost/circular_buffer.hpp>