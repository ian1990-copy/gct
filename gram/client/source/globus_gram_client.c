/******************************************************************************
gram_client.c

Description:
    Resource Managemant Client API's

    This file contains the Resource Management Client API funtion
    calls.  The resource management API provides functions for 
    submitting a job request to a RM, for asking when a job
    (submitted or not) might run, for cancelling a request,
    for requesting notification of state changes for a request,
    and for checking for pending notifications.

CVS Information:

    $Source$
    $Date$
    $Revision$
    $Author$
******************************************************************************/

/******************************************************************************
                             Include header files
******************************************************************************/
#include <stdio.h>
#include <malloc.h>
#include <sys/param.h>
#include <sys/time.h>
#include <nexus.h>
#include "gram.h"
#include "grami_rsl.h"
#include "gram_job_manager.h"
#if defined(TARGET_ARCH_SOLARIS)
#include <netdb.h>
#endif

/******************************************************************************
                               Type definitions
******************************************************************************/
typedef struct
{
    nexus_mutex_t mutex;
    nexus_cond_t cond;
    volatile nexus_bool_t done;
    int job_status;
    char job_contact_str[1000];
} graml_job_request_monitor_s;

typedef struct
{
    nexus_mutex_t mutex;
    nexus_cond_t cond;
    volatile nexus_bool_t done;
    int start_time_estimate;
    int start_time_interval_size;
} graml_start_time_monitor_s;

typedef struct
{
    gram_callback_func_t callback_func;
    nexus_endpoint_t endpoint;
} callback_s;

/******************************************************************************
                          Module specific prototypes
******************************************************************************/
static void 
graml_write_callback(void * arg,
                     int fd,
                     char * buf,
                     size_t nbytes);

static void 
graml_write_error_callback(void * arg,
                           int fd,
                           char * buf,
                           size_t nbytes,
                           int error);

static int 
graml_callback_attach_approval(void * user_arg,
                               char * url,
                               nexus_startpoint_t * sp);

static void 
graml_job_request_reply_handler(nexus_endpoint_t * endpoint,
                                nexus_buffer_t * buffer,
                                nexus_bool_t is_non_threaded);

static void 
graml_callback_handler(nexus_endpoint_t * endpoint,
                       nexus_buffer_t * buffer,
                       nexus_bool_t is_non_threaded);

static void 
graml_start_time_callback_handler(nexus_endpoint_t * endpoint,
                                  nexus_buffer_t * buffer,
                                  nexus_bool_t is_non_threaded);

/******************************************************************************
                       Define module specific variables
******************************************************************************/
static nexus_handler_t gram_job_request_reply_handler_table[] =
{
    {NEXUS_HANDLER_TYPE_NON_THREADED,
       graml_job_request_reply_handler},
};

static nexus_handler_t callback_handler_table[] =
{
    {NEXUS_HANDLER_TYPE_NON_THREADED,
       graml_callback_handler},
};

static nexus_handler_t gram_start_time_handler_table[] =
{
    {NEXUS_HANDLER_TYPE_NON_THREADED,
     graml_start_time_callback_handler},
};


/******************************************************************************
Function:	gram_job_request()
Description:
Parameters:
Returns:
******************************************************************************/
int 
gram_job_request(char * gatekeeper_url,
                 const char * description,
                 const int job_state_mask,
                 const char * callback_url,
                 char ** job_contact)
{
    int                          size;
    int                          contact_msg_size;
    int                          count;
    int                          rc;
    int                          gatekeeper_fd;
    char *                       gatekeeper_host;
    unsigned short               gatekeeper_port = 0;
    nexus_byte_t                 type;
    nexus_byte_t *               contact_msg_buffer;
    nexus_byte_t *               tmp_buffer;
    nexus_endpointattr_t         reply_epattr;
    nexus_endpoint_t             reply_ep;
    nexus_startpoint_t           reply_sp;
    graml_job_request_monitor_s  job_request_monitor;

    printf("in gram_job_request()\n");

    nexus_mutex_init(&job_request_monitor.mutex, (nexus_mutexattr_t *) NULL);
    nexus_cond_init(&job_request_monitor.cond, (nexus_condattr_t *) NULL);
    job_request_monitor.done = NEXUS_FALSE;

    nexus_endpointattr_init(&reply_epattr);
    nexus_endpointattr_set_handler_table(&reply_epattr,
					 gram_job_request_reply_handler_table,
					 1);
    nexus_endpoint_init(&reply_ep, &reply_epattr);
    nexus_endpoint_set_user_pointer(&reply_ep, &job_request_monitor);
    nexus_startpoint_bind(&reply_sp, &reply_ep);

    type  = (nexus_byte_t)(NEXUS_DC_FORMAT_LOCAL);
    size  = nexus_sizeof_byte(1);
    size += nexus_sizeof_int(1);
    size += nexus_sizeof_int(1);
    size += nexus_sizeof_char(strlen(description));
    size += nexus_sizeof_int(1);
    size += nexus_sizeof_int(1);
    size += nexus_sizeof_char(strlen(callback_url));
    size += nexus_sizeof_startpoint(&reply_sp, 1);

    if (size >= GRAM_MAX_MSG_SIZE)
        return (GRAM_ERROR_INVALID_REQUEST);
    /*
     * contact_msg_size includes the extra int added to the front of the 
     * message.
     * size is the size of the message without the extra int.
     */
    contact_msg_size = size + 4;
    tmp_buffer = (nexus_byte_t *)malloc(contact_msg_size);
    contact_msg_buffer = tmp_buffer;
    
    /*
     * Put 4-byte big-endian unsigned integer into front of message, to be
     * peeled off by the gram_gatekeeper
     */
    *tmp_buffer++ = (nexus_byte_t) (((size) & 0xFF000000) >> 24);
    *tmp_buffer++ = (nexus_byte_t) (((size) & 0xFF0000) >> 16);
    *tmp_buffer++ = (nexus_byte_t) (((size) & 0xFF00) >> 8);
    *tmp_buffer++ = (nexus_byte_t)  ((size) & 0xFF);

    /*
     * Pack the rest of the message that goes to the gram_job_manager
     */
    *tmp_buffer++ = (nexus_byte_t) type;
    nexus_user_put_int(&tmp_buffer, &size, 1);
    count= strlen(description);
    nexus_user_put_int(&tmp_buffer, &count, 1);
    nexus_user_put_char(&tmp_buffer, description, strlen(description));
    nexus_user_put_int(&tmp_buffer, &job_state_mask, 1);
    count= strlen(callback_url);
    nexus_user_put_int(&tmp_buffer, &count, 1);
    nexus_user_put_char(&tmp_buffer, callback_url, strlen(callback_url));
    nexus_user_put_startpoint_transfer(&tmp_buffer, &reply_sp, 1);
  
    if (nexus_split_nexus_url(gatekeeper_url,
                              &gatekeeper_host,
                              &gatekeeper_port,
                              NULL) != 0)
    {
        fprintf(stderr, " invalid url.\n");
        return (1);
    }

    /* Connecting to the gatekeeper.
     */
    rc = nexus_fd_connect(gatekeeper_host, gatekeeper_port, &gatekeeper_fd);
    if (rc != 0)
    {
        fprintf(stderr, " nexus_fd_connect failed.  rc = %d\n", rc);
        return (0);
    }

    /* Do gss authentication here */

    rc = nexus_fd_register_for_write(gatekeeper_fd,
                                    (char *) contact_msg_buffer,
                                     contact_msg_size,
                                     graml_write_callback,
                                     graml_write_error_callback,
                                     (void *) &job_request_monitor);
    if (rc != 0)
    {
        fprintf(stderr, "nexus_fd_register_for_write failed\n");
        return (GRAM_ERROR_PROTOCOL_FAILED);
    }

    nexus_mutex_lock(&job_request_monitor.mutex);
    while (!job_request_monitor.done)
    {
        nexus_cond_wait(&job_request_monitor.cond, &job_request_monitor.mutex);
    }
    nexus_mutex_unlock(&job_request_monitor.mutex);

    nexus_mutex_destroy(&job_request_monitor.mutex);
    nexus_cond_destroy(&job_request_monitor.cond);

    if (job_request_monitor.job_status == 0)
    {
        *job_contact = (char *) 
           malloc(strlen(job_request_monitor.job_contact_str) + 1);
/*
        sprintf(*job_contact, "%s", job_request_monitor.job_contact_str);
*/
        strcpy(*job_contact, job_request_monitor.job_contact_str);
        free(contact_msg_buffer);
    }

    return(job_request_monitor.job_status);

} /* gram_job_request() */

/******************************************************************************
Function:	graml_write_error_callback()
Description:
Parameters:
Returns:
******************************************************************************/
static void 
graml_write_error_callback(void * arg,
                           int fd,
                           char * buf,
                           size_t nbytes,
                           int error)
{
    graml_job_request_monitor_s *job_request_monitor = 
      (graml_job_request_monitor_s *) arg;

    job_request_monitor->job_status = GRAM_ERROR_PROTOCOL_FAILED;

    nexus_mutex_lock(&job_request_monitor->mutex);
    job_request_monitor->done = NEXUS_TRUE;
    nexus_cond_signal(&job_request_monitor->cond);
    nexus_mutex_unlock(&job_request_monitor->mutex);
} /* graml_write_error_callback() */

/******************************************************************************
Function:	graml_write_callback()
Description:
Parameters:
Returns:
******************************************************************************/
static void 
graml_write_callback(void * arg,
                     int fd,
                     char * buf,
                     size_t nbytes)
{
    printf("in graml_write_callback()\n");
} /* graml_write_callback() */

/******************************************************************************
Function:	graml_job_request_reply_handler()
Description:
Parameters:
Returns:
******************************************************************************/
static void 
graml_job_request_reply_handler(nexus_endpoint_t * endpoint,
                                nexus_buffer_t * buffer,
                                nexus_bool_t is_non_threaded)
{
    int              size;
    int              count = 0;
    int              format;
    nexus_byte_t     bformat;
    nexus_byte_t *   ptr;
    graml_job_request_monitor_s * job_request_monitor;

    job_request_monitor = nexus_endpoint_get_user_pointer(endpoint);

    printf("in graml_job_request_reply_handler()\n");

    nexus_get_int(buffer, &job_request_monitor->job_status, 1);
    if (job_request_monitor->job_status == 0)
    {
        nexus_get_int(buffer, &count, 1);
        nexus_get_char(buffer, job_request_monitor->job_contact_str, count);
    }

    *(job_request_monitor->job_contact_str+count)= '\0';

    /* got all of the message */
    nexus_mutex_lock(&job_request_monitor->mutex);
    job_request_monitor->done = NEXUS_TRUE;
    nexus_cond_signal(&job_request_monitor->cond);
    nexus_mutex_unlock(&job_request_monitor->mutex);
} /* graml_job_request_reply_handler() */

/******************************************************************************
Function:	gram_job_check()
Description:
Parameters:
Returns:
******************************************************************************/
int 
gram_job_check(char * gatekeeper_url,
               const char * description,
               float required_confidence,
               gram_time_t * estimate,
               gram_time_t * interval_size)
{
    return(0);
} /* gram_job_check() */

/******************************************************************************
Function:	gram_job_cancel()
Description:	sending cancel request to job manager
Parameters:
Returns:
******************************************************************************/
int 
gram_job_cancel(char * job_contact)
{
    int                rc;
    nexus_buffer_t     buffer;
    nexus_startpoint_t sp_to_job_manager;

    printf("in gram_job_cancel()\n");

    rc = nexus_attach(job_contact, &sp_to_job_manager);
    if (rc != 0)
    {
        printf("nexus_attach returned %d\n", rc);
        return (1);
    }
    nexus_buffer_init(&buffer, 1, 0);
    rc = nexus_send_rsr(&buffer,
                        &sp_to_job_manager,
                        CANCEL_HANDLER_ID,
                        NEXUS_TRUE,
                        NEXUS_FALSE);

    nexus_startpoint_destroy(&sp_to_job_manager);

    if (rc != 0)
    {
        return (GRAM_ERROR_PROTOCOL_FAILED);
    }
    else
    {
        return (GRAM_SUCCESS);
    }

} /* gram_job_cancel */ 

/******************************************************************************
Function:	gram_callback_allow()
Description:	
Parameters:
Returns:
******************************************************************************/
int 
gram_callback_allow(gram_callback_func_t callback_func,
                    char ** callback_contact)
{
    int			  rc;
    unsigned short 	  port = 0;
    char * 		  host;
    callback_s *	  callback;
    nexus_endpointattr_t  epattr;
    char * 		  tmp_contact;

    printf("in gram_callback_allow()\n");

    callback = (callback_s *) malloc(sizeof(callback_s));
    callback->callback_func = (gram_callback_func_t) callback_func;
    nexus_endpointattr_init(&epattr);
    nexus_endpointattr_set_handler_table(&epattr, callback_handler_table, 1);
    nexus_endpoint_init(&(callback->endpoint), &epattr);
    nexus_endpoint_set_user_pointer(&(callback->endpoint), callback);
    
    rc = nexus_allow_attach(&port, &host,
	     	            graml_callback_attach_approval,
		            (void *) callback);
       
    if (rc != 0)
    {
        printf("nexus_allow_attach returned %d\n", rc);
        return (1);
    }

    /* add 13 for x-nexus stuff plus 1 for the null */
    tmp_contact = (char *) malloc(sizeof(port) + MAXHOSTNAMELEN + 13);

    sprintf(tmp_contact, "x-nexus://%s:%hu/", host, port);
    *callback_contact = tmp_contact;

    return(0);

} /* gram_callback_allow() */


/******************************************************************************
Function:	graml_callback_attach_approval()
Description:	
Parameters:
Returns:
******************************************************************************/
static int
graml_callback_attach_approval(void * user_arg,
                                   char * url,
                                   nexus_startpoint_t * sp)
{
    callback_s * callback = (callback_s *) user_arg;

    printf("in graml_callback_attach_approval()\n");

    nexus_startpoint_bind(sp, &(callback->endpoint));

    return(0);
} /* graml_callback_attach_approval() */

/******************************************************************************
Function:	graml_callback_handler()
Description:	
Parameters:
Returns:
******************************************************************************/
static void 
graml_callback_handler(nexus_endpoint_t * endpoint,
                       nexus_buffer_t * buffer,
                       nexus_bool_t is_non_threaded)
{
    int count;
    int state;
    int errorcode;
    callback_s * callback;
    char job_contact[GRAM_MAX_MSG_SIZE];

    printf("in graml_callback_handler()\n");

    callback = (callback_s *) nexus_endpoint_get_user_pointer(endpoint);
    
    nexus_get_int(buffer, &count, 1);
    nexus_get_char(buffer, job_contact, count);
    *(job_contact+count)= '\0';
    nexus_get_int(buffer, &state, 1);
    nexus_get_int(buffer, &errorcode, 1);
    
    (*callback->callback_func)(job_contact, state, errorcode);
} /* graml_callback_handler() */

/******************************************************************************
Function:	gram_job_start_time()
Description:	
Parameters:
Returns:
******************************************************************************/
int 
gram_job_start_time(char * job_contact,
                    float required_confidence,
                    gram_time_t * estimate,
                    gram_time_t * interval_size)
{
    int                        rc;
    int                        size;
    nexus_buffer_t             buffer;
    nexus_startpoint_t         sp_to_job_manager;
    nexus_startpoint_t         sp;
    nexus_endpoint_t           ep;
    nexus_endpointattr_t       epattr;
    graml_start_time_monitor_s  start_time_monitor;

    printf("in gram_job_start_time()\n");

    nexus_mutex_init(&start_time_monitor.mutex, (nexus_mutexattr_t *) NULL);
    nexus_cond_init(&start_time_monitor.cond, (nexus_condattr_t *) NULL);

    nexus_mutex_lock(&start_time_monitor.mutex);
    start_time_monitor.done = NEXUS_FALSE;
    nexus_mutex_unlock(&start_time_monitor.mutex);

    nexus_endpointattr_init(&epattr);
    nexus_endpointattr_set_handler_table(&epattr,
                                         gram_start_time_handler_table,
                                         1);
    nexus_endpoint_init(&ep, &epattr);
    nexus_endpoint_set_user_pointer(&ep, &start_time_monitor);
    nexus_startpoint_bind(&sp, &ep);

    rc = nexus_attach(job_contact, &sp_to_job_manager);
    if (rc != 0)
    {
        printf("nexus_attach returned %d\n", rc);
        return (GRAM_ERROR_PROTOCOL_FAILED);
    }

    size  = nexus_sizeof_float(1);
    size += nexus_sizeof_startpoint(&sp, 1);

    nexus_buffer_init(&buffer, size, 0);
    nexus_put_float(&buffer, &required_confidence, 1);
    nexus_put_startpoint_transfer(&buffer, &sp, 1);

    rc = nexus_send_rsr(&buffer,
                        &sp_to_job_manager,
                        START_TIME_HANDLER_ID,
                        NEXUS_TRUE,
                        NEXUS_FALSE);

    if (rc != 0)
    {
        printf("nexus_send_rsr returned %d\n", rc);
        return (GRAM_ERROR_PROTOCOL_FAILED);
    }

    nexus_startpoint_destroy(&sp_to_job_manager);

    nexus_mutex_lock(&start_time_monitor.mutex);
    while (!start_time_monitor.done)
    {
        nexus_cond_wait(&start_time_monitor.cond, &start_time_monitor.mutex);
    }
    nexus_mutex_unlock(&start_time_monitor.mutex);

    nexus_mutex_destroy(&start_time_monitor.mutex);
    nexus_cond_destroy(&start_time_monitor.cond);

    estimate->dumb_time = start_time_monitor.start_time_estimate;
    interval_size->dumb_time = start_time_monitor.start_time_interval_size;
 
    return (GRAM_SUCCESS);

} /* gram_job_start_time() */

/******************************************************************************
Function:	graml_start_time_callback_handler()
Description:	
Parameters:
Returns:
******************************************************************************/
static void 
graml_start_time_callback_handler(nexus_endpoint_t * endpoint,
                                  nexus_buffer_t * buffer,
                                  nexus_bool_t is_non_threaded)
{
    graml_start_time_monitor_s * start_time_monitor;

    start_time_monitor = nexus_endpoint_get_user_pointer(endpoint);

    printf("in graml_start_time_callback_handler()\n");

    nexus_get_int(buffer, &start_time_monitor->start_time_estimate, 1);
    nexus_get_int(buffer, &start_time_monitor->start_time_interval_size, 1);
    
    nexus_mutex_lock(&start_time_monitor->mutex);
    start_time_monitor->done = NEXUS_TRUE;
    nexus_cond_signal(&start_time_monitor->cond);
    nexus_mutex_unlock(&start_time_monitor->mutex);

} /* graml_start_time_callback_handler() */

/******************************************************************************
Function:	gram_callback_check()
Description:	
Parameters:
Returns:
******************************************************************************/
int 
gram_callback_check()
{
    printf("in gram_callback_check()\n");

    nexus_poll();

    return(0);

} /* gram_callback_check() */

/******************************************************************************
Function:	gram_job_contact_free()
Description:	
Parameters:
Returns:
******************************************************************************/
int 
gram_job_contact_free(char * job_contact)
{
    printf("in gram_job_contact_free()\n");

    free(job_contact);
} /* gram_job_contact_free() */
