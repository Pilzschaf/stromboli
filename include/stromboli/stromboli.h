#ifndef STROMBOLI_H
#define STROMBOLI_H

#include <grounded/string/grounded_string.h>
#include <volk/volk.h>

#define STROMBOLI_SUCCESS() ((StromboliResult){0})
#define STROMBOLI_MAKE_ERROR(code, text) ((StromboliResult){code, STR8_LITERAL(text)})
#define STROMBOLI_ERROR(result) (result.error)
#define STROMBOLI_NO_ERROR(result) (!STROMBOLI_ERROR(result))

typedef struct StromboliContext {
    VkInstance instance;
} StromboliContext;

typedef enum StromboliErrorCode {
    STROMBOLI_SUCCESS = 0,
    STROMBOLI_VOLK_INITIALIZE_ERROR,
    STROMBOLI_INSTANCE_CREATE_ERROR,
} StromboliErrorCode;

typedef struct StromboliResult {
    int error;
    String8 errorString;
} StromboliResult;

typedef struct StromboliInitializationParameters {
    String8 applicationName;
    u32 applicationMajorVersion;
    u32 applicationMinorVersion;
    u32 applicationPatchVersion;
    u32 vulkanApiVersion;
} StromboliInitializationParameters;

StromboliResult initStromboli(StromboliContext* context, StromboliInitializationParameters* parameters);
void shutdownStromboli(StromboliContext* context);

#endif // STROMBOLI_H