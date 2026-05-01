/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/**
 * class CallCredentials
 * @see https://github.com/grpc/grpc/tree/master/src/php/ext/grpc/call_credentials.c
 */

#include "call_credentials.h"

#include <ext/spl/spl_exceptions.h>
#include <zend_exceptions.h>

#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "call.h"

zend_class_entry *grpc_ce_call_credentials;
PHP_GRPC_DECLARE_OBJECT_HANDLER(call_credentials_ce_handlers)

/* Frees and destroys an instance of wrapped_grpc_call_credentials */
PHP_GRPC_FREE_WRAPPED_FUNC_START(wrapped_grpc_call_credentials)
  if (p->wrapped != NULL) {
    grpc_call_credentials_release(p->wrapped);
  }
  if (Z_TYPE(p->php_callback) != IS_UNDEF) {
    zval_ptr_dtor(&p->php_callback);
  }
PHP_GRPC_FREE_WRAPPED_FUNC_END()

/* Thread-safe no-op plugin used by ChannelCredentials::createComposite in the
 * pure-PHP path. Returns empty metadata synchronously without touching PHP
 * memory, so it is safe to call from any thread including event_engine threads.
 * The actual auth metadata is injected by AbstractCall::_applyCallCredentials
 * on the PHP thread before OP_SEND_INITIAL_METADATA is sent. */
int grpc_php_pure_noop_get_metadata(
    void *ptr, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t *num_creds_md, grpc_status_code *status,
    const char **error_details) {
  *num_creds_md = 0;
  *status = GRPC_STATUS_OK;
  *error_details = NULL;
  return 1;  /* synchronous */
}
void grpc_php_pure_noop_destroy(void *ptr) {}

/* Initializes an instance of wrapped_grpc_call_credentials to be
 * associated with an object of a class specified by class_type */
php_grpc_zend_object create_wrapped_grpc_call_credentials(
    zend_class_entry *class_type TSRMLS_DC) {
  PHP_GRPC_ALLOC_CLASS_OBJECT(wrapped_grpc_call_credentials);
  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  /* ecalloc zeroes memory; ZVAL_UNDEF (type=0) is already set, but be explicit */
  ZVAL_UNDEF(&intern->php_callback);
  PHP_GRPC_FREE_CLASS_OBJECT(wrapped_grpc_call_credentials,
                             call_credentials_ce_handlers);
}

zval *grpc_php_wrap_call_credentials(grpc_call_credentials
                                     *wrapped TSRMLS_DC) {
  zval *credentials_object;
  PHP_GRPC_MAKE_STD_ZVAL(credentials_object);
  object_init_ex(credentials_object, grpc_ce_call_credentials);
  wrapped_grpc_call_credentials *credentials =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call_credentials,
                                credentials_object);
  credentials->wrapped = wrapped;
  ZVAL_UNDEF(&credentials->php_callback);
  return credentials_object;
}

/**
 * Create composite credentials from two existing credentials.
 * @param CallCredentials $cred1_obj The first credential
 * @param CallCredentials $cred2_obj The second credential
 * @return CallCredentials The new composite credentials object
 */
PHP_METHOD(CallCredentials, createComposite) {
  zval *cred1_obj;
  zval *cred2_obj;

  /* "OO" == 2 Objects */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "OO", &cred1_obj,
                            grpc_ce_call_credentials, &cred2_obj,
                            grpc_ce_call_credentials) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "createComposite expects 2 CallCredentials",
                         1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_call_credentials *cred1 =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call_credentials, cred1_obj);
  wrapped_grpc_call_credentials *cred2 =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call_credentials, cred2_obj);

  /* Pure-PHP call credentials (wrapped==NULL) cannot be composed at the C
   * level. Direct composition is only supported in legacy mode. */
  if (Z_TYPE(cred1->php_callback) != IS_UNDEF ||
      Z_TYPE(cred2->php_callback) != IS_UNDEF) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "CallCredentials::createComposite is not supported "
                         "with pure-PHP call credentials; use "
                         "ChannelCredentials::createComposite instead",
                         1 TSRMLS_CC);
    return;
  }

  grpc_call_credentials *creds =
    grpc_composite_call_credentials_create(cred1->wrapped, cred2->wrapped,
                                           NULL);
  zval *creds_object = grpc_php_wrap_call_credentials(creds TSRMLS_CC);
  RETURN_DESTROY_ZVAL(creds_object);
}

/**
 * Create a call credentials object from the plugin API.
 *
 * When GRPC_PHP_PURE_CALL_CREDENTIALS=1 the callback is stored on the PHP
 * object and no C plugin is registered, avoiding the ZTS crash in
 * plugin_get_metadata (issue #41917). The callback is applied on the PHP
 * thread by AbstractCall::_applyCallCredentials before every RPC send.
 *
 * @param callable $callback The metadata callback function
 * @return CallCredentials The new call credentials object
 */
PHP_METHOD(CallCredentials, createFromPlugin) {
  if (grpc_php_is_pure_call_creds_enabled()) {
    zval *callback_zval;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z",
                              &callback_zval) == FAILURE ||
        !zend_is_callable(callback_zval, 0, NULL)) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "createFromPlugin expects 1 callback",
                           1 TSRMLS_CC);
      return;
    }
    zval *creds_object;
    PHP_GRPC_MAKE_STD_ZVAL(creds_object);
    object_init_ex(creds_object, grpc_ce_call_credentials);
    wrapped_grpc_call_credentials *credentials =
      PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call_credentials, creds_object);
    credentials->wrapped = NULL;
    ZVAL_COPY(&credentials->php_callback, callback_zval);
    RETURN_DESTROY_ZVAL(creds_object);
  }

  /* Legacy path: register the C plugin that invokes PHP on the event_engine
   * thread. Safe on NTS PHP; crashes on ZTS (issue #41917). */
  zend_fcall_info *fci;
  zend_fcall_info_cache *fci_cache;

  fci = (zend_fcall_info *)malloc(sizeof(zend_fcall_info));
  fci_cache = (zend_fcall_info_cache *)malloc(sizeof(zend_fcall_info_cache));
  memset(fci, 0, sizeof(zend_fcall_info));
  memset(fci_cache, 0, sizeof(zend_fcall_info_cache));

  /* "f" == 1 function */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "f*", fci, fci_cache,
                            fci->params, fci->param_count) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "createFromPlugin expects 1 callback", 1 TSRMLS_CC);
    free(fci);
    free(fci_cache);
    return;
  }

  plugin_state *state;
  state = (plugin_state *)malloc(sizeof(plugin_state));
  memset(state, 0, sizeof(plugin_state));

  /* save the user provided PHP callback function */
  state->fci = fci;
  state->fci_cache = fci_cache;

  grpc_metadata_credentials_plugin plugin;
  plugin.get_metadata = plugin_get_metadata;
  plugin.destroy = plugin_destroy_state;
  plugin.state = (void *)state;
  plugin.type = "";
  // TODO(yihuazhang): Expose min_security_level via the PHP API so that
  // applications can decide what minimum security level their plugins require.
  grpc_call_credentials *creds =
    grpc_metadata_credentials_create_from_plugin(plugin, GRPC_PRIVACY_AND_INTEGRITY, NULL);
  zval *creds_object = grpc_php_wrap_call_credentials(creds TSRMLS_CC);
  RETURN_DESTROY_ZVAL(creds_object);
}

/**
 * Return the stored PHP callback, or null when in legacy mode.
 * Used by AbstractCall::setCallCredentials to detect pure-PHP creds.
 * @return callable|null
 */
PHP_METHOD(CallCredentials, getPhpCallCredentialsCallback) {
  wrapped_grpc_call_credentials *creds =
    PHP_GRPC_GET_WRAPPED_OBJECT(wrapped_grpc_call_credentials, getThis());
  if (Z_TYPE(creds->php_callback) == IS_UNDEF) {
    RETURN_NULL();
  }
  RETURN_ZVAL(&creds->php_callback, 1, 0);
}

/* Callback function for plugin creds API */
int plugin_get_metadata(
    void *ptr, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t *num_creds_md, grpc_status_code *status,
    const char **error_details) {
  TSRMLS_FETCH();

  plugin_state *state = (plugin_state *)ptr;

  /* prepare to call the user callback function with info from the
   * grpc_auth_metadata_context */
  zval *arg;
  PHP_GRPC_MAKE_STD_ZVAL(arg);
  object_init(arg);
  php_grpc_add_property_string(arg, "service_url", context.service_url, true);
  php_grpc_add_property_string(arg, "method_name", context.method_name, true);
  zval *retval = NULL;
  PHP_GRPC_MAKE_STD_ZVAL(retval);
  state->fci->params = arg;
  state->fci->retval = retval;
  state->fci->param_count = 1;

  PHP_GRPC_DELREF(arg);

  /* call the user callback function */
  PHP_GRPC_CALL_FUNCTION(state->fci, state->fci_cache);

  *num_creds_md = 0;
  *status = GRPC_STATUS_OK;
  *error_details = NULL;

  bool should_return = false;
  grpc_metadata_array metadata;

  if (retval == NULL || Z_TYPE_P(retval) != IS_ARRAY) {
    *status = GRPC_STATUS_INVALID_ARGUMENT;
    should_return = true;  // Synchronous return.
  }
  if (!create_metadata_array(retval, &metadata)) {
    *status = GRPC_STATUS_INVALID_ARGUMENT;
    should_return = true;  // Synchronous return.
    grpc_php_metadata_array_destroy_including_entries(&metadata);
  }

  if (retval != NULL) {
    zval_ptr_dtor(arg);
    zval_ptr_dtor(retval);
    PHP_GRPC_FREE_STD_ZVAL(arg);
    PHP_GRPC_FREE_STD_ZVAL(retval);
  }
  if (should_return) {
    return true;
  }

  if (metadata.count > GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX) {
    *status = GRPC_STATUS_INTERNAL;
    *error_details = gpr_strdup(
        "PHP plugin credentials returned too many metadata entries");
    for (size_t i = 0; i < metadata.count; i++) {
      // TODO(stanleycheung): Why don't we need to unref the key here?
      grpc_slice_unref(metadata.metadata[i].value);
    }
  } else {
    // Return data to core.
    *num_creds_md = metadata.count;
    for (size_t i = 0; i < metadata.count; ++i) {
      creds_md[i] = metadata.metadata[i];
    }
  }

  grpc_metadata_array_destroy(&metadata);
  return true;  // Synchronous return.
}

/* Cleanup function for plugin creds API */
void plugin_destroy_state(void *ptr) {
  plugin_state *state = (plugin_state *)ptr;
  free(state->fci);
  free(state->fci_cache);
  free(state);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_createComposite, 0, 0, 2)
  ZEND_ARG_INFO(0, creds1)
  ZEND_ARG_INFO(0, creds2)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_createFromPlugin, 0, 0, 1)
  ZEND_ARG_INFO(0, callback)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_getPhpCallCredentialsCallback, 0, 0, 0)
ZEND_END_ARG_INFO()

static zend_function_entry call_credentials_methods[] = {
  PHP_ME(CallCredentials, createComposite, arginfo_createComposite,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(CallCredentials, createFromPlugin, arginfo_createFromPlugin,
         ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
  PHP_ME(CallCredentials, getPhpCallCredentialsCallback,
         arginfo_getPhpCallCredentialsCallback, ZEND_ACC_PUBLIC)
  PHP_FE_END
};

void grpc_init_call_credentials(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\CallCredentials", call_credentials_methods);
  ce.create_object = create_wrapped_grpc_call_credentials;
  grpc_ce_call_credentials = zend_register_internal_class(&ce TSRMLS_CC);
  PHP_GRPC_INIT_HANDLER(wrapped_grpc_call_credentials,
                        call_credentials_ce_handlers);
}
