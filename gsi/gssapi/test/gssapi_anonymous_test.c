#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "gssapi.h"
#include "globus_gss_assist.h"
#include "globus_error_openssl.h"

void globus_print_error(globus_result_t              error_result);

int main()
{
    OM_uint32                           init_maj_stat;
    OM_uint32                           accept_maj_stat;
    OM_uint32                           maj_stat;
    OM_uint32                           min_stat;
    OM_uint32                           ret_flags;
    OM_uint32                           req_flags;
    gss_buffer_desc                     send_tok;
    gss_buffer_desc                     recv_tok;
    gss_buffer_desc *                   token_ptr;
    gss_buffer_desc                     output_name;
    gss_OID                             mech_type;
    gss_name_t                          target_name;
    gss_name_t                          source_name;
    gss_ctx_id_t                        init_context;
    gss_ctx_id_t                        accept_context;
    gss_ctx_id_t                        del_init_context;
    gss_ctx_id_t                        del_accept_context;
    gss_cred_id_t                       delegated_cred;
    gss_cred_id_t                       cred_handle;
    char *                              error_str;

    /* Initialize variables */
    
    token_ptr = GSS_C_NO_BUFFER;
    init_context = GSS_C_NO_CONTEXT;
    accept_context = GSS_C_NO_CONTEXT;
    del_init_context = GSS_C_NO_CONTEXT;
    del_accept_context = GSS_C_NO_CONTEXT;
    delegated_cred = GSS_C_NO_CREDENTIAL;
    accept_maj_stat = GSS_S_CONTINUE_NEEDED;
    req_flags = GSS_C_ANON_FLAG|GSS_C_CONF_FLAG;
    ret_flags = 0;

    globus_module_activate(GLOBUS_GSI_GSSAPI_MODULE);
    
    /* acquire the credential */

    maj_stat = gss_acquire_cred(&min_stat,
                                NULL,
                                GSS_C_INDEFINITE,
                                GSS_C_NO_OID_SET,
                                GSS_C_BOTH,
                                &cred_handle,
                                NULL,
                                NULL);

    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gss_assist_display_status_str(&error_str,
                                             NULL,
                                             maj_stat,
                                             min_stat,
                                             0);
        printf("\nLINE %d ERROR: %s\n", __LINE__, error_str);
        globus_print_error((globus_result_t) min_stat);
        exit(1);
    }

    /* get the subject name */
    
    maj_stat = gss_inquire_cred(&min_stat, 
                                cred_handle,
                                &target_name,
                                NULL,
                                NULL,
                                NULL);
    
    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gss_assist_display_status_str(&error_str,
                                             NULL,
                                             maj_stat,
                                             min_stat,
                                             0);
        printf("\nLINE %d ERROR: %s\n", __LINE__, error_str);
        globus_print_error((globus_result_t) min_stat);
        exit(1);
    }

    /* set up the first security context */
    
    init_maj_stat = gss_init_sec_context(&min_stat,
                                         GSS_C_NO_CREDENTIAL,
                                         &init_context,
                                         target_name,
                                         GSS_C_NULL_OID,
                                         req_flags,
                                         0,
                                         GSS_C_NO_CHANNEL_BINDINGS,
                                         token_ptr,
                                         NULL,
                                         &send_tok,
                                         NULL,
                                         NULL);

    if(init_maj_stat != GSS_S_CONTINUE_NEEDED)
    {
        globus_gss_assist_display_status_str(&error_str,
                                             NULL,
                                             init_maj_stat,
                                             min_stat,
                                             0);
        printf("\nLINE %d ERROR: %s\n", __LINE__, error_str);
        globus_print_error((globus_result_t) min_stat);
        exit(1);
    }

    while(1)
    {
        accept_maj_stat=gss_accept_sec_context(&min_stat,
                                               &accept_context,
                                               GSS_C_NO_CREDENTIAL,
                                               &send_tok, 
                                               GSS_C_NO_CHANNEL_BINDINGS,
                                               &source_name,
                                               &mech_type,
                                               &recv_tok,
                                               &ret_flags,
                                               /* ignore time_rec */
                                               NULL, 
                                               GSS_C_NO_CREDENTIAL);

        if(accept_maj_stat != GSS_S_COMPLETE &&
           accept_maj_stat != GSS_S_CONTINUE_NEEDED)
        {
            globus_gss_assist_display_status_str(&error_str,
                                                 NULL,
                                                 init_maj_stat,
                                                 min_stat,
                                                 0);
            printf("\nLINE %d ERROR: %s\n", __LINE__, error_str);
            globus_print_error((globus_result_t) min_stat);
            exit(1);
        }
        else if(accept_maj_stat == GSS_S_COMPLETE)
        {
            break;
        }

        init_maj_stat = gss_init_sec_context(&min_stat,
                                             GSS_C_NO_CREDENTIAL,
                                             &init_context,
                                             target_name,
                                             GSS_C_NULL_OID,
                                             req_flags,
                                             0,
                                             GSS_C_NO_CHANNEL_BINDINGS,
                                             &recv_tok,
                                             NULL,
                                             &send_tok,
                                             NULL,
                                             NULL);
        
        
        if(init_maj_stat != GSS_S_COMPLETE &&
           init_maj_stat != GSS_S_CONTINUE_NEEDED)
        {
            globus_gss_assist_display_status_str(&error_str,
                                                 NULL,
                                                 init_maj_stat,
                                                 min_stat,
                                                 0);
            printf("\nLINE %d ERROR: %s\n", __LINE__, error_str);
            globus_print_error((globus_result_t) min_stat);
            exit(1);
        }
    }

    printf("%s:%d: Successfully established anonymous security context\n",
           __FILE__,
           __LINE__);


    maj_stat = gss_display_name(&min_stat,
                                source_name,
                                (gss_buffer_t) &output_name,
                                NULL);

    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gss_assist_display_status_str(&error_str,
                                             NULL,
                                             maj_stat,
                                             min_stat,
                                             0);
        printf("\nLINE %d ERROR: %s\n", __LINE__, error_str);
        globus_print_error((globus_result_t) min_stat);
        exit(1);
    }

    printf("%s:%d: Received subject name: %s\n",
           __FILE__,
           __LINE__,
           (char*) output_name.value);

    maj_stat = gss_delete_sec_context(&min_stat,
                                      init_context,
                                      GSS_C_NO_BUFFER);
    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gss_assist_display_status_str(&error_str,
                                             NULL,
                                             maj_stat,
                                             min_stat,
                                             0);
        printf("\nLINE %d ERROR: %s\n", __LINE__, error_str);
        globus_print_error((globus_result_t) min_stat);
        exit(1);
    }

    maj_stat = gss_delete_sec_context(&min_stat,
                                      accept_context,
                                      GSS_C_NO_BUFFER);
    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gss_assist_display_status_str(&error_str,
                                             NULL,
                                             maj_stat,
                                             min_stat,
                                             0);
        printf("\nLINE %d ERROR: %s\n", __LINE__, error_str);
        globus_print_error((globus_result_t) min_stat);
        exit(1);
    }

    maj_stat = gss_release_name(&min_stat,
                                &target_name);
    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gss_assist_display_status_str(&error_str,
                                             NULL,
                                             maj_stat,
                                             min_stat,
                                             0);
        printf("\nLINE %d ERROR: %s\n", __LINE__, error_str);
        globus_print_error((globus_result_t) min_stat);
        exit(1);
    }

    maj_stat = gss_release_cred(&min_stat,
                                &cred_handle);
    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gss_assist_display_status_str(&error_str,
                                             NULL,
                                             maj_stat,
                                             min_stat,
                                             0);
        printf("\nLINE %d ERROR: %s\n", __LINE__, error_str);
        globus_print_error((globus_result_t) min_stat);
        exit(1);
    }

    maj_stat = gss_release_buffer(&min_stat,
                                &output_name);
    if(maj_stat != GSS_S_COMPLETE)
    {
        globus_gss_assist_display_status_str(&error_str,
                                             NULL,
                                             maj_stat,
                                             min_stat,
                                             0);
        printf("\nLINE %d ERROR: %s\n", __LINE__, error_str);
        globus_print_error((globus_result_t) min_stat);
        exit(1);
    }

    globus_module_deactivate(GLOBUS_GSI_GSSAPI_MODULE);

    exit(0);
}

void globus_print_error(
    globus_result_t                     error_result)
{
    globus_object_t *                   error_obj = NULL;
    globus_object_t *                   base_error_obj = NULL;
    char *                              error_string = NULL;
    
    error_obj = globus_error_get(error_result);
    error_string = globus_error_print_chain(error_obj);
    globus_libc_fprintf(stderr, "%s\n", error_string);
    globus_libc_free(error_string);
    globus_object_free(error_obj);
};
