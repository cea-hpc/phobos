#include "pho_common.h"

#include <pthread.h>
#include <string.h>

static struct phobos_global_context *PHO_CONTEXT;

/* must be called before calling any other phobos function */
int pho_context_init(void)
{
    if (PHO_CONTEXT)
        LOG_RETURN(-EALREADY, "global state already initialized");

    PHO_CONTEXT = xcalloc(1, sizeof(*PHO_CONTEXT));

    PHO_CONTEXT->log_level = PHO_LOG_DEFAULT;
    pho_log_callback_set(NULL); /* set default log callback */
    PHO_CONTEXT->log_dev_output = false;
    pthread_mutex_init(&PHO_CONTEXT->config.lock, NULL);
    pho_context_reset_mock_functions();

    return 0;
}

void pho_context_fini(void)
{
    pthread_mutex_destroy(&PHO_CONTEXT->config.lock);
    free(PHO_CONTEXT);
    PHO_CONTEXT = NULL;
}

struct phobos_global_context *phobos_context(void)
{
    return PHO_CONTEXT;
}

void phobos_module_context_set(struct phobos_global_context *context)
{
    PHO_CONTEXT = context;
}

void pho_context_reset_mock_functions(void)
{
    PHO_CONTEXT->mocks.mock_ioctl = NULL;
    PHO_CONTEXT->mocks.mock_ltfs.mock_mkdir = NULL;
    PHO_CONTEXT->mocks.mock_ltfs.mock_command_call = NULL;
    PHO_CONTEXT->mocks.mock_ltfs.mock_statfs = NULL;
    PHO_CONTEXT->mocks.mock_ltfs.mock_getxattr = NULL;
    PHO_CONTEXT->mocks.mock_ltfs.mock_setxattr = NULL;
    PHO_CONTEXT->mocks.mock_failure_after_second_partial_release = NULL;
}
