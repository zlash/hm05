project('hm05', 'cpp', default_options: ['cpp_std=c++11'])

libftdi = dependency('libftdi')

executable('hm05', ['src/hm05.cpp','src/cart_comm.cpp'], dependencies: libftdi)

