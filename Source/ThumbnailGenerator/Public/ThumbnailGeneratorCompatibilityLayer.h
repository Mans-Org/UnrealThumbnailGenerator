// Copyright Mans Isaksson. All Rights Reserved.

#pragma once

#include "Runtime/Launch/Resources/Version.h"

#define ENGINE_VERSION_HIGHER_THAN(Major, Minor) ENGINE_MAJOR_VERSION > Major || (ENGINE_MAJOR_VERSION == Major && ENGINE_MINOR_VERSION > Minor)

#define ENGINE_VERSION_LESS_THAN(Major, Minor) ENGINE_MAJOR_VERSION < Major || (ENGINE_MAJOR_VERSION == Major && ENGINE_MINOR_VERSION < Minor)
