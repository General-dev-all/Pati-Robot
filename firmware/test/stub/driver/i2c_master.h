// Konak testi icin sahte i2c_master.h
//
// NEDEN VAR: goz karsilastirma testi pati_gozler.cpp'yi oldugu gibi ice
// aliyor ve o dosya pati_guc.hpp'yi (guc kaynagi, pil yuzdesi) ice
// aliyor — o da gercek IDF basligini istiyor.
//
// Testin I2C ile isi yok: cizilen pikselleri tarayiciyla
// karsilastiriyor. Burada yalnizca TIPIN var olmasi yetiyor.

#pragma once

typedef struct i2c_master_bus_t* i2c_master_bus_handle_t;
