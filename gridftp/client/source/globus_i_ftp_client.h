#ifndef GLOBUS_DONT_DOCUMENT_INTERNAL
/**
 * @file globus_i_ftp_client.h
 * Globus FTP Client Library
 *
 * $RCSfile$
 * $Revision$
 * $Date $
 */

#include "globus_common.h"
#include "globus_ftp_client.h"
#include "globus_ftp_client_plugin.h"
#include "globus_error_string.h"

#ifndef GLOBUS_L_INCLUDE_FTP_CLIENT_H
#define GLOBUS_L_INCLUDE_FTP_CLIENT_H

#ifndef EXTERN_C_BEGIN
#ifdef __cplusplus
#define EXTERN_C_BEGIN extern "C" {
#define EXTERN_C_END }
#else
#define EXTERN_C_BEGIN
#define EXTERN_C_END
#endif
#endif

EXTERN_C_BEGIN


#ifdef DEBUG_LOCKS
#define globus_i_ftp_client_handle_lock(handle) \
	printf("locking handle %p at %s:%d\n", (handle), __FILE__, __LINE__), \
	globus_mutex_lock(&(handle)->mutex)
#define globus_i_ftp_client_handle_unlock(handle) \
	printf("unlocking handle %p at %s:%d\n", (handle), __FILE__, __LINE__), \
	globus_mutex_unlock(&(handle)->mutex)
#define globus_i_ftp_client_data_target_lock(data_target) \
	printf("locking data_target %p at %s:%d\n", (data_target), __FILE__, __LINE__), \
	globus_mutex_lock(&(data_target)->mutex)
#define globus_i_ftp_client_data_target_unlock(data_target) \
	printf("unlocking data_target %p at %s:%d\n", (data_target), __FILE__, __LINE__), \
	globus_mutex_unlock(&(data_target)->mutex)
#else
#define globus_i_ftp_client_handle_lock(handle) \
	globus_mutex_lock(&handle->mutex)
#define globus_i_ftp_client_handle_unlock(handle) \
	globus_mutex_unlock(&handle->mutex)
#define globus_i_ftp_client_data_target_lock(data_target) \
	globus_mutex_lock(&(data_target)->mutex)
#define globus_i_ftp_client_data_target_unlock(data_target) \
	globus_mutex_unlock(&(data_target)->mutex)
#endif

/**
 * Client handle magic number.
 *
 * Modify this if the handle data structure is changed.
 */
#define GLOBUS_FTP_CLIENT_MAGIC_STRING "FTPClient-1.0"

#ifdef BUILD_DEBUG
#define GLOBUS_I_FTP_CLIENT_BAD_MAGIC(h) \
    (!(h && (*h) && \
       !strcmp(((*h))->magic, \
	   GLOBUS_FTP_CLIENT_MAGIC_STRING)))
#else
#define GLOBUS_I_FTP_CLIENT_BAD_MAGIC(h) 0
#endif

/*
 * Attributes
 */

/**
 * The globus_i_ftp_client_operationattr_t is a pointer to this structure type.
 */
typedef struct globus_i_ftp_client_operationattr_t
{
    globus_ftp_control_parallelism_t            parallelism;
    globus_bool_t				force_striped;
    globus_ftp_control_layout_t                 layout;
    globus_ftp_control_tcpbuffer_t              buffer;
    globus_bool_t                               using_default_auth;
    globus_ftp_control_auth_info_t              auth_info;
    globus_ftp_control_type_t                   type;
    globus_ftp_control_mode_t                   mode;
    globus_bool_t                               append;
    globus_ftp_control_dcau_t                   dcau;
    globus_ftp_control_protection_t             data_prot;
    globus_bool_t                               resume_third_party;
    globus_bool_t                               read_all;
    globus_ftp_client_data_callback_t           read_all_intermediate_callback;
    void *                                      read_all_intermediate_callback_arg;
}
globus_i_ftp_client_operationattr_t;

/**
 * Byte range report.
 * @ingroup globus_ftp_client_operationattr
 *
 * This structure contains information about a single extent of data
 * stored on an FTP server. A report structure is generated from each
 * part of an extended-block mode restart marker message from an FTP
 * server.
 */
typedef struct
{
    /**
     *  Minimum value of this range.
     */
    globus_off_t				offset;

    /**
     * Maximum value of this range.
     */
    globus_off_t				end_offset;
}
globus_i_ftp_client_range_t;

/**
 * Handle attributes.
 * @ingroup globus_ftp_client_handleattr
 */
typedef struct globus_i_ftp_client_handleattr_t
{
    /**
     * Cache all connections.
     *
     * This attribute is used to cause the ftp client library to keep
     * all control (and where possible) data connections open between
     * ftp operations.
     */
    globus_bool_t                               cache_all;

    /** 
     * List of cached URLs.
     *
     * This list is used to manage the URL cache which is manipulated
     * by the user calling globus_ftp_client_handle_cache_url_state()
     * and globus_ftp_client_handle_flush_url_state().
     */
    globus_list_t *                             url_cache;
    /**
     * List of plugin structures.
     *
     * This list contains all plugins which can be associated
     * with an ftp client handle. These plugins will be notified when
     * operations are done using a handle associated with them.
     */
    globus_list_t *                             plugins;

    /*
     *  NETLOGGER
     */
    globus_netlogger_handle_t *                 nl_handle;
}
globus_i_ftp_client_handleattr_t;

/* Handles */
/**
 * Handle state machine.
 */
typedef enum
{
    GLOBUS_FTP_CLIENT_HANDLE_START,
    GLOBUS_FTP_CLIENT_HANDLE_SOURCE_CONNECT,
    GLOBUS_FTP_CLIENT_HANDLE_SOURCE_SETUP_CONNECTION,
    GLOBUS_FTP_CLIENT_HANDLE_SOURCE_LIST,
    GLOBUS_FTP_CLIENT_HANDLE_SOURCE_NLST,
    GLOBUS_FTP_CLIENT_HANDLE_SOURCE_RETR_OR_ERET,
    GLOBUS_FTP_CLIENT_HANDLE_DEST_CONNECT,
    GLOBUS_FTP_CLIENT_HANDLE_DEST_SETUP_CONNECTION,
    GLOBUS_FTP_CLIENT_HANDLE_DEST_STOR_OR_ESTO,
    GLOBUS_FTP_CLIENT_HANDLE_ABORT,
    GLOBUS_FTP_CLIENT_HANDLE_RESTART,
    GLOBUS_FTP_CLIENT_HANDLE_FAILURE,
    /* This is called when one side of a third-party transfer has sent
     * it's positive or negative response to the transfer, but the
     * other hasn't yet done the same.
     */
    GLOBUS_FTP_CLIENT_HANDLE_THIRD_PARTY_TRANSFER,
    GLOBUS_FTP_CLIENT_HANDLE_THIRD_PARTY_TRANSFER_ONE_COMPLETE,
    GLOBUS_FTP_CLIENT_HANDLE_FINALIZE
}
globus_ftp_client_handle_state_t;

/**
 * Supported operation types.
 */
typedef enum
{
    GLOBUS_FTP_CLIENT_IDLE,
    GLOBUS_FTP_CLIENT_DELETE,
    GLOBUS_FTP_CLIENT_MKDIR,
    GLOBUS_FTP_CLIENT_RMDIR,
    GLOBUS_FTP_CLIENT_MOVE,
    GLOBUS_FTP_CLIENT_LIST,
    GLOBUS_FTP_CLIENT_NLST,
    GLOBUS_FTP_CLIENT_GET,
    GLOBUS_FTP_CLIENT_PUT,
    GLOBUS_FTP_CLIENT_TRANSFER,
    GLOBUS_FTP_CLIENT_MDTM,
    GLOBUS_FTP_CLIENT_SIZE
}
globus_i_ftp_client_operation_t;

typedef enum
{
    GLOBUS_FTP_CLIENT_FALSE = GLOBUS_FALSE,
    GLOBUS_FTP_CLIENT_TRUE  = GLOBUS_TRUE,
    GLOBUS_FTP_CLIENT_MAYBE
}
globus_ftp_client_tristate_t;

typedef enum
{
    GLOBUS_FTP_CLIENT_TARGET_START, 
    GLOBUS_FTP_CLIENT_TARGET_CONNECT,
    GLOBUS_FTP_CLIENT_TARGET_AUTHENTICATE,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_SITE_FAULT,
    GLOBUS_FTP_CLIENT_TARGET_SITE_FAULT,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_SITE_HELP,
    GLOBUS_FTP_CLIENT_TARGET_SITE_HELP,
    GLOBUS_FTP_CLIENT_TARGET_FEAT,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_CONNECTION,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_TYPE,
    GLOBUS_FTP_CLIENT_TARGET_TYPE,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_MODE,
    GLOBUS_FTP_CLIENT_TARGET_MODE,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_SIZE,
    GLOBUS_FTP_CLIENT_TARGET_SIZE,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_DCAU,
    GLOBUS_FTP_CLIENT_TARGET_DCAU,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_PBSZ,
    GLOBUS_FTP_CLIENT_TARGET_PBSZ,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_PROT,
    GLOBUS_FTP_CLIENT_TARGET_PROT,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_BUFSIZE,
    GLOBUS_FTP_CLIENT_TARGET_BUFSIZE,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_REMOTE_RETR_OPTS,
    GLOBUS_FTP_CLIENT_TARGET_REMOTE_RETR_OPTS,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_LOCAL_RETR_OPTS,
    GLOBUS_FTP_CLIENT_TARGET_LOCAL_RETR_OPTS,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_PASV,
    GLOBUS_FTP_CLIENT_TARGET_PASV,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_PORT,
    GLOBUS_FTP_CLIENT_TARGET_PORT,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_REST_STREAM,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_REST_EB,
    GLOBUS_FTP_CLIENT_TARGET_REST,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_OPERATION,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_LIST,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_NLST,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_GET,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_PUT,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_TRANSFER_SOURCE,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_TRANSFER_DEST,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_DELETE,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_MKDIR,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_RMDIR,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_RNFR,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_RNTO,
    GLOBUS_FTP_CLIENT_TARGET_SETUP_MDTM,
    GLOBUS_FTP_CLIENT_TARGET_LIST,
    GLOBUS_FTP_CLIENT_TARGET_NLST,
    GLOBUS_FTP_CLIENT_TARGET_RETR,
    GLOBUS_FTP_CLIENT_TARGET_STOR,
    GLOBUS_FTP_CLIENT_TARGET_MDTM,
    GLOBUS_FTP_CLIENT_TARGET_READY_FOR_DATA,
    GLOBUS_FTP_CLIENT_TARGET_NEED_LAST_BLOCK,
    GLOBUS_FTP_CLIENT_TARGET_NEED_EMPTY_QUEUE,
    GLOBUS_FTP_CLIENT_TARGET_NEED_COMPLETE,
    GLOBUS_FTP_CLIENT_TARGET_COMPLETED_OPERATION,
    GLOBUS_FTP_CLIENT_TARGET_NOOP,
    GLOBUS_FTP_CLIENT_TARGET_FAULT,
    GLOBUS_FTP_CLIENT_TARGET_CLOSED
}
globus_ftp_client_target_state_t;

/**
 * FTP server features we are interested in. 
 *
 * Upon a new connection, we will attempt to probe via the SITE HELP
 * and FEAT commands which the server supports. If we can't determine
 * from that, we'll try using the command and find out if the server
 * supports it.
 *
 */
typedef enum
{
    /* Buffer-size setting commands; keep these at the beginning of
     * the enum
     */
    GLOBUS_FTP_CLIENT_FEATURE_RETRBUFSIZE = 0,
    GLOBUS_FTP_CLIENT_FEATURE_RBUFSZ,
    GLOBUS_FTP_CLIENT_FEATURE_RBUFSIZ,
    GLOBUS_FTP_CLIENT_FEATURE_STORBUFSIZE,
    GLOBUS_FTP_CLIENT_FEATURE_SBUSSZ,
    GLOBUS_FTP_CLIENT_FEATURE_SBUFSIZ,
    GLOBUS_FTP_CLIENT_FEATURE_BUFSIZE,
    GLOBUS_FTP_CLIENT_FEATURE_SBUF,
    GLOBUS_FTP_CLIENT_FEATURE_ABUF,

    GLOBUS_FTP_CLIENT_FEATURE_REST_STREAM,
    GLOBUS_FTP_CLIENT_FEATURE_PARALLELISM,
    GLOBUS_FTP_CLIENT_FEATURE_DCAU,
    GLOBUS_FTP_CLIENT_FEATURE_ESTO,
    GLOBUS_FTP_CLIENT_FEATURE_ERET,
    GLOBUS_FTP_CLIENT_FEATURE_SIZE,
    GLOBUS_FTP_CLIENT_FEATURE_MAX,
    GLOBUS_FTP_CLIENT_LAST_BUFFER_COMMAND = GLOBUS_FTP_CLIENT_FEATURE_ABUF,
    GLOBUS_FTP_CLIENT_FIRST_FEAT_FEATURE = GLOBUS_FTP_CLIENT_FEATURE_SBUF
}
globus_i_ftp_client_probed_feature_t;

typedef struct
{
    struct globus_i_ftp_client_target_s *	source;
    struct globus_i_ftp_client_target_s *	dest;
    globus_i_ftp_client_operation_t		operation;
}
globus_i_ftp_client_data_target_t;

typedef struct globus_i_ftp_client_handle_t
{
    /** client handle magic number */
    char                                        magic[24];

    /** The user's handle pointer used to initialize this structure */
    globus_ftp_client_handle_t *		handle;

    /**
     * Information about the connection to the source URL for a get
     * or third-party transfer.
     */ 
    struct globus_i_ftp_client_target_s *       source;

    /** source URL */
    char *                                      source_url;

    /**
     * Information about the connection to the destination URL for a put
     * or third-party transfer.
     */ 
    struct globus_i_ftp_client_target_s *       dest;
    /** destination URL */
    char *                                      dest_url;
    globus_i_ftp_client_handleattr_t            attr;

    /** Current operation on this handle */
    globus_i_ftp_client_operation_t             op;

    /** Callback to be called once this operation is completed. */
    globus_ftp_client_complete_callback_t       callback;
    /** User-supplied parameter to this callback */
    void *                                      callback_arg;

    globus_ftp_client_handle_state_t            state;

    /** 
     * Priority queue of data blocks which haven't yet been sent 
     * to the FTP control library
     */
    globus_priority_q_t                         stalled_blocks;
    
    /**
     * Hash of data blocks which are currently being processed by the
     * control handle.
     */
    globus_hashtable_t                          active_blocks;

    /**
     * Number of blocks in the active_blocks hash.
     */
    int                                         num_active_blocks;
    /**
     * Address of PASV side of a transfer.
     */
    globus_ftp_control_host_port_t *            pasv_address;
    /**
     * Number of passive addresses we know about.
     */
    int                                         num_pasv_addresses;

    /** Error object to pass to the completion callback */
    globus_object_t *                           err;

    /**
     * Restart information.
     */
    struct globus_i_ftp_client_restart_s *      restart_info;

    /**
     * Delayed notification information.
     */
    int                                         notify_in_progress;
    globus_bool_t                               notify_abort;
    globus_bool_t                               notify_restart;

    /** Size of the file to be downloaded, if known. */
    globus_off_t                                source_size;

    /** Current information about what has been transferred so far. */
    globus_ftp_client_restart_marker_t          restart_marker;

    /** Partial file transfer starting offset. */
    globus_off_t                                partial_offset;
    /** Partial file transfer ending offset. */
    globus_off_t                                partial_end_offset;

        /*** added by bresnaha ***/
    char *                                      eret_alg_str;
    char *                                      esto_alg_str;
    /*** end add by bresnaha ***/
 
    /** Base offset for a transfer, to be added to all offsets in
     * stream mode
     */
    globus_off_t                                base_offset;
    globus_off_t                                read_all_biggest_offset;

    /** Pointer to user's modification time buffer */
    globus_abstime_t *				modification_time_pointer;

    /** Pointer to user's size buffer */
    globus_off_t *				size_pointer;

    globus_mutex_t                              mutex;

    void *                                      user_pointer;
}
globus_i_ftp_client_handle_t;

/** 
 * FTP Connection State.
 *
 * This type is used to store information about an active FTP control
 * connection. This information includes the FTP control handle, the
 * extensions which the server supports, and the current session
 * settings which have been set on the control handle.
 */
typedef struct globus_i_ftp_client_target_s
{
    /** Current connection/activity state of this target */
    globus_ftp_client_target_state_t		state;

    /** Handle to an FTP control connection. */
    globus_ftp_control_handle_t *		control_handle;
    /** URL we are currently processing. */
    char *					url_string;
    /** Host/port we are connected to. */
    globus_url_t				url;
    /** Information about server authentication. */
    globus_ftp_control_auth_info_t		auth_info;

    /** Features we've discovered about this target so far. */
    globus_ftp_client_tristate_t		
					features[GLOBUS_FTP_CLIENT_FEATURE_MAX];
    /** Current settings */
    globus_ftp_control_dcau_t			dcau;
    globus_ftp_control_protection_t		data_prot;
    globus_ftp_control_type_t			type;
    globus_ftp_control_tcpbuffer_t		tcp_buffer;
    globus_ftp_control_mode_t			mode;
    globus_ftp_control_structure_t		structure;
    globus_ftp_control_layout_t			layout;
    globus_ftp_control_parallelism_t		parallelism;

    /** Requested settings */
    globus_i_ftp_client_operationattr_t *	attr;

    /** The client that this target is associated with */
    globus_i_ftp_client_handle_t *		owner;

    /** Data connection caching information */
    globus_i_ftp_client_data_target_t		cached_data_conn;
    
    /** Plugin mask associated with the currently pending command. */
    globus_ftp_client_plugin_command_mask_t	mask;
} globus_i_ftp_client_target_t;

/**
 * URL caching support structure.
 *
 * This structure is used to implement the cache of URLs. When a
 * target is needed, the client library first checks the handle's
 * cache. If the target associated with the url is available, and it
 * matches the security attributes of the operation being performed,
 * it will be used for the operation.
 *
 * The current implementation only allows for a URL to be cached only
 * once per handle.
 *
 * The cache manipulations are done by the API functions
 * globus_ftp_client_cache_url_state() and
 * globus_ftp_client_flush_url_state(), and the internal functions
 * globus_i_ftp_client_target_find() and
 * globus_i_ftp_client_target_release().
 */
typedef struct
{
    /** URL which the user has requested to be cached. */
    globus_url_t				url;
    /** 
     * Target which matches that URL. If this is NULL, then the cache
     * entry is empty.
     */
    globus_i_ftp_client_target_t *		target;
}
globus_i_ftp_client_cache_entry_t;

/**
 * Restart information management.
 */
typedef struct globus_i_ftp_client_restart_s
{
    char *					source_url;
    globus_i_ftp_client_operationattr_t *	source_attr;
    char *					dest_url;
    globus_i_ftp_client_operationattr_t *	dest_attr;
    globus_ftp_client_restart_marker_t		marker;
    globus_abstime_t				when;
    globus_callback_handle_t			callback_handle;
}
globus_i_ftp_client_restart_t;

/**
 * @struct globus_ftp_client_plugin_t
 * Plugin.
 * @ingroup globus_ftp_client_plugins
 *
 * Each plugin implementation should define a method for initializing
 * one of these structures. Plugins may be implemented as either a
 * static function table, or a specialized plugin with plugin-specific 
 * attributes.
 *
 * Each plugin function may be either GLOBUS_NULL, or a valid function 
 * pointer. If the function is GLOBUS_NULL, then the plugin will not
 * be notified when the corresponding event happens.
 */
typedef struct globus_i_ftp_client_plugin_t
{
    /** 
     * Plugin name.
     *
     * The plugin name is used by the FTP Client library to detect
     * multiple instances of the same plugin being associated with
     * a #globus_ftp_client_handleattr_t or #globus_ftp_client_handle_t.
     * 
     * Each plugin type should have a unique plugin name, which must
     * be a NULL-terminated string of arbitrary length.
     */
    char *					plugin_name;

    /**
     * The value the user/plugin implementation passed into the plugin
     * handling parts of the API.
     */
    globus_ftp_client_plugin_t *		plugin;

    /**
     * Plugin function pointers.
     */
    globus_ftp_client_plugin_copy_t		copy_func;
    /**
     * Plugin function pointers.
     */
    globus_ftp_client_plugin_destroy_t		destroy_func; 

    globus_ftp_client_plugin_delete_t		delete_func;
    globus_ftp_client_plugin_mkdir_t		mkdir_func;
    globus_ftp_client_plugin_rmdir_t		rmdir_func;
    globus_ftp_client_plugin_move_t		move_func;
    globus_ftp_client_plugin_verbose_list_t     verbose_list_func;
    globus_ftp_client_plugin_list_t		list_func;
    globus_ftp_client_plugin_get_t		get_func;
    globus_ftp_client_plugin_put_t		put_func;
    globus_ftp_client_plugin_third_party_transfer_t
						third_party_transfer_func;

    globus_ftp_client_plugin_modification_time_t
						modification_time_func;
    globus_ftp_client_plugin_size_t		size_func;
    globus_ftp_client_plugin_abort_t		abort_func;
    globus_ftp_client_plugin_connect_t		connect_func;
    globus_ftp_client_plugin_authenticate_t	authenticate_func;
    globus_ftp_client_plugin_read_t		read_func;
    globus_ftp_client_plugin_write_t		write_func;
    globus_ftp_client_plugin_data_t		data_func;

    globus_ftp_client_plugin_command_t		command_func;
    globus_ftp_client_plugin_response_t		response_func;
    globus_ftp_client_plugin_fault_t		fault_func;
    globus_ftp_client_plugin_complete_t		complete_func;

    /** 
     * Command Mask
     *
     * The bits set in this mask determine which command responses the plugin
     * is interested in. The command_mask should be a bitwise-or of
     * the values in the globus_ftp_client_plugin_command_mask_t enumeration.
     */
    globus_ftp_client_plugin_command_mask_t	command_mask;

    /** This pointer is reserved for plugin-specific data */
    void *					plugin_specific;
} globus_i_ftp_client_plugin_t;

#ifndef DOXYGEN
/* globus_ftp_client_attr.c */
globus_result_t
globus_i_ftp_client_handleattr_copy(
    globus_i_ftp_client_handleattr_t *		dest,
    globus_i_ftp_client_handleattr_t *		src);

int
globus_i_ftp_client_plugin_list_search(
    void *					datum,
    void *					arg);
/* globus_ftp_client.c */
void
globus_i_ftp_client_handle_is_active(globus_ftp_client_handle_t *handle);

void
globus_i_ftp_client_handle_is_not_active(globus_ftp_client_handle_t *handle);

void
globus_i_ftp_client_control_is_active(globus_ftp_control_handle_t * handle);

void
globus_i_ftp_client_control_is_not_active(globus_ftp_control_handle_t * handle);

const char *
globus_i_ftp_op_to_string(
    globus_i_ftp_client_operation_t		op);

int
globus_i_ftp_client_count_digits(
    globus_off_t				num);

extern globus_ftp_control_auth_info_t globus_i_ftp_client_default_auth_info;

/* globus_ftp_client_handle.c */
globus_object_t *
globus_i_ftp_client_target_find(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_i_ftp_client_operationattr_t *	attr,
    globus_i_ftp_client_target_t **		target);

void
globus_i_ftp_client_target_release(
    globus_i_ftp_client_handle_t *		handle,
    globus_i_ftp_client_target_t *		target);

void
globus_i_ftp_client_restart_info_delete(
    globus_i_ftp_client_restart_t *		restart_info);

globus_bool_t
globus_i_ftp_client_can_reuse_data_conn(
    globus_i_ftp_client_handle_t *		client_handle);

globus_result_t
globus_i_ftp_client_cache_add(
    globus_list_t **				cache,
    const char *				url);

globus_result_t
globus_i_ftp_client_cache_remove(
    globus_list_t **				cache,
    const char *				url);

globus_result_t
globus_i_ftp_client_cache_destroy(
    globus_list_t **				cache);

/* globus_ftp_client_data.c  */
int
globus_i_ftp_client_data_cmp(
    void *					priority_1,
    void *					priority_2);

globus_object_t *
globus_i_ftp_client_data_dispatch_queue(
    globus_i_ftp_client_handle_t *		handle);

void
globus_i_ftp_client_data_flush(
    globus_i_ftp_client_handle_t *		handle);

/* globus_ftp_client_transfer.c */
void
globus_i_ftp_client_transfer_complete(
    globus_i_ftp_client_handle_t *		client_handle);

globus_object_t *
globus_i_ftp_client_restart(
    globus_i_ftp_client_handle_t *		handle,
    globus_i_ftp_client_restart_t *		restart_info);

/* globus_ftp_client_plugin.c */

void
globus_i_ftp_client_plugin_notify_list(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_i_ftp_client_operationattr_t *	attr);

void
globus_i_ftp_client_plugin_notify_verbose_list(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_i_ftp_client_operationattr_t *	attr);

void
globus_i_ftp_client_plugin_notify_delete(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_i_ftp_client_operationattr_t *	attr);

void
globus_i_ftp_client_plugin_notify_mkdir(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_i_ftp_client_operationattr_t *	attr);

void
globus_i_ftp_client_plugin_notify_rmdir(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_i_ftp_client_operationattr_t *	attr);

void
globus_i_ftp_client_plugin_notify_move(
    globus_i_ftp_client_handle_t *		handle,
    const char *				source_url,
    const char *				dest_url,
    globus_i_ftp_client_operationattr_t *	attr);

void
globus_i_ftp_client_plugin_notify_get(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_i_ftp_client_operationattr_t *	attr,
    const globus_ftp_client_restart_marker_t *	restart);

void
globus_i_ftp_client_plugin_notify_put(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_i_ftp_client_operationattr_t *	attr,
    const globus_ftp_client_restart_marker_t *	restart);

void
globus_i_ftp_client_plugin_notify_transfer(
    globus_i_ftp_client_handle_t *		handle,
    const char *				source_url,
    globus_i_ftp_client_operationattr_t *	source_attr,
    const char *				dest_url,
    globus_i_ftp_client_operationattr_t *	dest_attr,
    const globus_ftp_client_restart_marker_t *	restart);

void
globus_i_ftp_client_plugin_notify_modification_time(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_i_ftp_client_operationattr_t *	attr);

void
globus_i_ftp_client_plugin_notify_size(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_i_ftp_client_operationattr_t *	attr);

void
globus_i_ftp_client_plugin_notify_restart(
    globus_i_ftp_client_handle_t *		handle);

void
globus_i_ftp_client_plugin_notify_connect(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url);

void
globus_i_ftp_client_plugin_notify_authenticate(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    const globus_ftp_control_auth_info_t *	auth_info);

void
globus_i_ftp_client_plugin_notify_abort(
    globus_i_ftp_client_handle_t *		handle);

void
globus_i_ftp_client_plugin_notify_read(
    globus_i_ftp_client_handle_t *		handle,
    const globus_byte_t *			buffer,
    globus_size_t				buffer_length);

void
globus_i_ftp_client_plugin_notify_write(
    globus_i_ftp_client_handle_t *		handle,
    const globus_byte_t *			buffer,
    globus_size_t				buffer_length,
    globus_off_t				offset,
    globus_bool_t				eof);

void
globus_i_ftp_client_plugin_notify_data(
    globus_i_ftp_client_handle_t *		handle,
    globus_object_t *				error,
    const globus_byte_t *			buffer,
    globus_size_t				buffer_length,
    globus_off_t				offset,
    globus_bool_t				eof);

void
globus_i_ftp_client_plugin_notify_command(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_ftp_client_plugin_command_mask_t	command_mask,
    const char *				command_spec,
    ...);

void
globus_i_ftp_client_plugin_notify_response(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_ftp_client_plugin_command_mask_t	command_mask,
    globus_object_t *				error,
    const globus_ftp_control_response_t *	ftp_response);

void
globus_i_ftp_client_plugin_notify_fault(
    globus_i_ftp_client_handle_t *		handle,
    const char *				url,
    globus_object_t *				error);

void
globus_i_ftp_client_plugin_notify_complete(
    globus_i_ftp_client_handle_t *		handle);

/* globus_ftp_client_restart.c */
globus_object_t *
globus_i_ftp_client_restart_register_oneshot(
    globus_i_ftp_client_handle_t *		handle);

/* globus_ftp_client_transfer.c */
void
globus_i_ftp_client_force_close_callback(
    void *					user_arg,
    globus_ftp_control_handle_t *		handle,
    globus_object_t *				error,
    globus_ftp_control_response_t *		response);

globus_object_t *
globus_i_ftp_client_target_activate(
    globus_i_ftp_client_handle_t *		handle,
    globus_i_ftp_client_target_t *		target,
    globus_bool_t *				registered);

/* globus_ftp_client_state.c */
void
globus_i_ftp_client_response_callback(
    void *					user_arg,
    globus_ftp_control_handle_t *		handle,
    globus_object_t *				error,
    globus_ftp_control_response_t *		response);
#endif

EXTERN_C_END

#endif /* GLOBUS_L_INCLUDE_FTP_CLIENT_H */

#endif /* GLOBUS_DONT_DOCUMENT_INTERNAL */
