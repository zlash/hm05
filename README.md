# HM05 - ROM flashing tool for DITTO mini flashcart

Only built and tested on linux. Should be almost-trivially portable when the need arises.

## Dependencies

* Meson
* libftdi-dev 

**Note:** C++20 support is required because of `__VA_OPT__`. The macro could be reworked for backwards compatibility.

## Building / Installing

1. Setup builddir with Meson. In the project directory run

    ```
    meson setup .build
    ```
2. Inside .build directory run

    ```
    meson compile
    ```