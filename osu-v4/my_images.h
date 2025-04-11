#ifndef MY_IMAGES_H
#define MY_IMAGES_H

#include "image1.h"
#include "image2.h"
#include "image3.h"

static const uint8_t* images[] = { image1, image2, image3 };
static const size_t imageSizes[] = { sizeof(image1), sizeof(image2), sizeof(image3) };
static const int imageCount = 3;

#endif
