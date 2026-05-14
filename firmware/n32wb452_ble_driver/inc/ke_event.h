/**
 ****************************************************************************************
 *
 * @file ke_event.h
 *
 * @brief This file contains the definition related to kernel events.
 *
 * Copyright (C) RivieraWaves 2009-2015
 *
 *
 ****************************************************************************************
 */

#ifndef _KE_EVENT_H_
#define _KE_EVENT_H_

/**
 ****************************************************************************************
 * @addtogroup EVT Events and Schedule
 * @ingroup KERNEL
 * @brief Event scheduling module.
 *
 * The KE_EVT module implements event scheduling functions. It can be used to
 * implement deferred actions.
 *
 * @{
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "rwip_config.h"          // stack configuration

#include <stdint.h>       // standard integer definition


/*
 * CONSTANTS
 ****************************************************************************************
 */


/// Status of ke_task API functions
enum KE_EVENT_STATUS
{
    KE_EVENT_OK = 0,
    KE_EVENT_FAIL,
    KE_EVENT_UNKNOWN,
    KE_EVENT_CAPA_EXCEEDED,
    KE_EVENT_ALREADY_EXISTS,
};


/*
 * TYPE DEFINITION
 ****************************************************************************************
 */




/*
 * FUNCTION PROTOTYPES
 ****************************************************************************************
 */



/**
 ****************************************************************************************
 * @brief Initialize Kernel event module.
 ****************************************************************************************
 */
#ifdef __cplusplus
extern "C" {
#endif
void ke_event_init(void);
#ifdef __cplusplus
}
#endif

/**
 ****************************************************************************************
 * @brief Register an event callback.
 *
 * @param[in]  event_type       Event type.
 * @param[in]  p_callback       Pointer to callback function.
 *
 * @return                      Status
 ****************************************************************************************
 */
#ifdef __cplusplus
extern "C" {
#endif
uint8_t ke_event_callback_set(uint8_t event_type, void (*p_callback)(void));
#ifdef __cplusplus
}
#endif

/**
 ****************************************************************************************
 * @brief Set an event
 *
 * This primitive sets one event. It will trigger the call to the corresponding event
 * handler in the next scheduling call.
 *
 * @param[in]  event_type      Event to be set.
 ****************************************************************************************
 */
#ifdef __cplusplus
extern "C" {
#endif
void ke_event_set(uint8_t event_type);
#ifdef __cplusplus
}
#endif

/**
 ****************************************************************************************
 * @brief Clear an event
 *
 * @param[in]  event_type      Event to be cleared.
 ****************************************************************************************
 */
#ifdef __cplusplus
extern "C" {
#endif
void ke_event_clear(uint8_t event_type);
#ifdef __cplusplus
}
#endif

/**
 ****************************************************************************************
 * @brief Get the status of an event
 *
 * @param[in]  event_type      Event to get.
 *
 * @return                     Event status (0: not set / 1: set)
 ****************************************************************************************
 */
#ifdef __cplusplus
extern "C" {
#endif
uint8_t ke_event_get(uint8_t event_type);
#ifdef __cplusplus
}
#endif

/**
 ****************************************************************************************
 * @brief Get all event status
 *
 * @return                     Events bit field
 ****************************************************************************************
 */
#ifdef __cplusplus
extern "C" {
#endif
uint32_t ke_event_get_all(void);
#ifdef __cplusplus
}
#endif

/**
 ****************************************************************************************
 * @brief Flush all pending events.
 ****************************************************************************************
 */
#ifdef __cplusplus
extern "C" {
#endif
void ke_event_flush(void);
#ifdef __cplusplus
}
#endif

/**
 ****************************************************************************************
 * @brief Event scheduler entry point.
 *
 * This primitive is the entry point of Kernel event scheduling.
 ****************************************************************************************
 */
#ifdef __cplusplus
extern "C" {
#endif
void ke_event_schedule(void);
#ifdef __cplusplus
}
#endif



/// @} EVT

#endif //_KE_EVENT_H_
