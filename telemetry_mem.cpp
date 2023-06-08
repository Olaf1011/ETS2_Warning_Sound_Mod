/**
 * @brief Example of using a shared memory to pass a mostly fixed set of telemetry
 * data to another process.
 *
 * Note that use of the shared memory reduces precision of the output and
 * increases latency between event and possible reaction.
 */

// Windows stuff.
#pragma comment(lib, "winmm.lib")
#define WINVER 0x0500
#define _WIN32_WINNT 0x0500
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>

// SDK

#include <fstream>

#include "scssdk_telemetry.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_telemetry_eut2.h"
#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_telemetry_ats.h"

#define UNUSED(x)

/**
 * @name Callbacks remembered from the initialization info.
 */
//@{
scs_telemetry_register_for_channel_t register_for_channel = NULL;
scs_telemetry_unregister_from_channel_t unregister_from_channel = NULL;
scs_log_t game_log = NULL;
//@}

/**
 * @brief Last timestamp we received.
 */
static scs_timestamp_t last_timestamp = static_cast<scs_timestamp_t>(-1);
static scs_timestamp_t current_timestamp = static_cast<scs_timestamp_t>(-1);


/**
 * @brief Prints message to game log.
 */
void log_line(const scs_log_type_t type, const char *const text, ...)
{
	if (! game_log) {
		return;
	}
	char formated[1000];

	va_list args;
	va_start(args, text);
	vsnprintf_s(formated, sizeof(formated), _TRUNCATE, text, args);
	formated[sizeof(formated) - 1] = 0;
	va_end(args);

	game_log(type, formated);
}

const size_t MAX_SUPPORTED_WHEEL_COUNT = 8;
static scs_u32_t time_over_limit = 0;
#pragma pack(push)
#pragma pack(1)

/**
 * @brief The layout of the shared memory.
*/
struct telemetry_state_t
{
	scs_u8_t				running;					// Is the telemetry running or it is paused?

	scs_float_t				throttle;					// SCS_TELEMETRY_TRUCK_CHANNEL_effective_throttle
	scs_float_t				speedometer_speed;			// SCS_TELEMETRY_TRUCK_CHANNEL_speed
	scs_float_t				new_max_speed;					
	scs_float_t				old_max_speed;			
}data;

#pragma pack(pop)

/**
 * @brief Handle of the memory mapping.
 */
HANDLE memory_mapping = NULL;

/**
 * @brief Block inside the shared memory.
 */
//telemetry_state_t *data = NULL;

/**
 * @brief Deinitialize the shared memory objects.
 */
void deinitialize_shared_memory(void)
{
}

/**
 * @brief Initialize the shared memory objects.
 */
bool initialize_shared_memory(void)
{
	// Setup the mapping.

	log_line(SCS_LOG_TYPE_message, "Initializing this pluging or what ever");

	{
		const std::ifstream f("plugins/toFast.wav");

		if (!f.good())
			log_line(SCS_LOG_TYPE_error, "Initializing this pluging or what ever");
	}
	{
		const std::ifstream f("plugins/speedChange.wav");

		if (!f.good())
			log_line(SCS_LOG_TYPE_error, "Initializing this pluging or what ever");
	}
	{
		const std::ifstream f("plugins/speedUp.wav");

		if (!f.good())
			log_line(SCS_LOG_TYPE_error, "Initializing this pluging or what ever");
	}
	// Defaults in the structure.

	memset(&data, 0, sizeof(data));

	// We are always initialized in the paused state.

	data.running = 0;

	return true;
}

/**
 * @brief Float storage callback.
 *
 * Can be used together with SCS_TELEMETRY_CHANNEL_FLAG_no_value in which case it
 * will store zero if the value is not available.
 */
SCSAPI_VOID telemetry_store_float(const scs_string_t name, const scs_u32_t index, const scs_value_t *const value, const scs_context_t context)
{
	assert(context);
	scs_float_t *const storage = static_cast<scs_float_t *>(context);

	if (value) {
		assert(value->type == SCS_VALUE_TYPE_float);
		*storage = value->value_float.value;
	}
	else {
		*storage = 0.0f;
	}
}

/**
 * @brief s32 storage callback.
 *
 * Can be used together with SCS_TELEMETRY_CHANNEL_FLAG_no_value in which case it
 * will store zero if the value is not available.
 */
SCSAPI_VOID telemetry_store_s32(const scs_string_t name, const scs_u32_t index, const scs_value_t *const value, const scs_context_t context)
{
	assert(context);
	scs_s32_t *const storage = static_cast<scs_s32_t *>(context);

	if (value) {
		assert(value->type == SCS_VALUE_TYPE_s32);
		*storage = value->value_s32.value;
	}
	else {
		*storage = 0;
	}
}

/**
 * @brief Orientation storage callback.
 *
 * Can be used together with SCS_TELEMETRY_CHANNEL_FLAG_no_value in which case it
 * will store zero if the value is not available.
 */
SCSAPI_VOID telemetry_store_orientation(const scs_string_t name, const scs_u32_t index, const scs_value_t *const value, const scs_context_t context)
{
	assert(context);
	scs_value_euler_t *const storage = static_cast<scs_value_euler_t *>(context);

	if (value) {
		assert(value->type == SCS_VALUE_TYPE_euler);
		*storage = value->value_euler;
	}
	else {
		storage->heading = 0.0f;
		storage->pitch = 0.0f;
		storage->roll = 0.0f;
	}
}

/**
 * @brief Vector storage callback.
 *
 * Can be used together with SCS_TELEMETRY_CHANNEL_FLAG_no_value in which case it
 * will store zero if the value is not available.
 */
SCSAPI_VOID telemetry_store_fvector(const scs_string_t name, const scs_u32_t index, const scs_value_t *const value, const scs_context_t context)
{
	assert(context);
	scs_value_fvector_t *const storage = static_cast<scs_value_fvector_t *>(context);

	if (value) {
		assert(value->type == SCS_VALUE_TYPE_fvector);
		*storage = value->value_fvector;
	}
	else {
		storage->x = 0.0f;
		storage->y = 0.0f;
		storage->z = 0.0f;
	}
}

/**
 * @brief Placement storage callback.
 *
 * Can be used together with SCS_TELEMETRY_CHANNEL_FLAG_no_value in which case it
 * will store zeros if the value is not available.
 */
SCSAPI_VOID telemetry_store_dplacement(const scs_string_t name, const scs_u32_t index, const scs_value_t *const value, const scs_context_t context)
{
	assert(context);
	scs_value_dplacement_t *const storage = static_cast<scs_value_dplacement_t *>(context);

	if (value) {
		assert(value->type == SCS_VALUE_TYPE_dplacement);
		*storage = value->value_dplacement;
	}
	else {
		storage->position.x = 0.0;
		storage->position.y = 0.0;
		storage->position.z = 0.0;
		storage->orientation.heading = 0.0f;
		storage->orientation.pitch = 0.0f;
		storage->orientation.roll = 0.0f;
	}
}

/**
 * @brief Finds attribute with specified name in the configuration structure.
 *
 * Returns NULL if the attribute was not found or if it is not of the expected type.
 */
const scs_named_value_t *find_attribute(const scs_telemetry_configuration_t &configuration, const char *const name, const scs_u32_t index, const scs_value_type_t expected_type)
{
	for (const scs_named_value_t *current = configuration.attributes; current->name; ++current) {
		if ((current->index != index) || (strcmp(current->name, name) != 0)) {
			continue;
		}
		if (current->value.type == expected_type) {
			return current;
		}
		log_line(SCS_LOG_TYPE_error, "Attribute %s has unexpected type %u", name, static_cast<unsigned>(current->value.type));
		break;
	}
	return NULL;
}

/**
 * @brief Called whenever the game pauses or unpauses its telemetry output.
 */
SCSAPI_VOID telemetry_pause(const scs_event_t event, const void *const UNUSED(event_info), const scs_context_t UNUSED(context))
{
	data.running = (event == SCS_TELEMETRY_EVENT_started) ? 1 : 0;
}

/**
 * @brief Called whenever configuration changes.
 */
SCSAPI_VOID telemetry_configuration(const scs_event_t event, const void *const event_info, const scs_context_t UNUSED(context))
{
	// We currently only care for the truck telemetry info.

	const struct scs_telemetry_configuration_t *const info = static_cast<const scs_telemetry_configuration_t *>(event_info);
	if (strcmp(info->id, SCS_TELEMETRY_CONFIG_truck) != 0) {
		return;
	}
}

/**
 * @brief Called whenever configuration changes.
 */
SCSAPI_VOID speed_check(const scs_event_t UNUSED(event), const void* const event_info, const scs_context_t UNUSED(context))
{
	constexpr float oneClick = 0.28f;
	constexpr scs_u32_t oneSecond = 1000000;
	const struct scs_telemetry_frame_start_t* const info = static_cast<const scs_telemetry_frame_start_t*>(event_info);
	// only check if the game is not paused
	if (data.running == 0) return;

	//Update only ever 1 second
	if (info->simulation_time > last_timestamp + oneSecond)
		last_timestamp = info->simulation_time;
	else
		return;

	if (data.new_max_speed < oneClick * 30.f)
		return;

	//Set the speed to the current speed so we don't get a sound effect on launch
	if (data.old_max_speed < oneClick)
	{
		data.old_max_speed = data.new_max_speed;
	}

	//Check if the speed limit decreased
	if(data.old_max_speed > (data.new_max_speed + oneClick))
	{
		data.old_max_speed = data.new_max_speed;
		PlaySound(TEXT("plugins/speedDown.wav"), NULL, SND_ASYNC);
	}
	//Check if the speed limit increased
	else if (data.old_max_speed < (data.new_max_speed -oneClick))
	{
		data.old_max_speed = data.new_max_speed;
		PlaySound(TEXT("plugins/speedUp.wav"), NULL, SND_ASYNC);
	}

	//Notify the user of speeding after 5 seconds
	if (data.speedometer_speed > data.new_max_speed + oneClick * 5 && data.throttle > 0.1f)
	{
		if(++time_over_limit > 5)
			PlaySound(TEXT("plugins/toFast.wav"), NULL, SND_ASYNC);
	}
	else
	{
		time_over_limit = 0;
	}
}

/**
 * @brief Telemetry API initialization function.
 *
 * See scssdk_telemetry.h
 */
SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t *const params)
{
	// We currently support only one version of the API.

	if (version != SCS_TELEMETRY_VERSION_1_00) {
		return SCS_RESULT_unsupported;
	}
	const scs_telemetry_init_params_v100_t *const version_params = static_cast<const scs_telemetry_init_params_v100_t *>(params);
	game_log = version_params->common.log;

	// Check application version.

	log_line(SCS_LOG_TYPE_message, "Game '%s' %u.%u", version_params->common.game_id, SCS_GET_MAJOR_VERSION(version_params->common.game_version), SCS_GET_MINOR_VERSION(version_params->common.game_version));

	if (strcmp(version_params->common.game_id, SCS_GAME_ID_EUT2) == 0) {

		// Below the minimum version there might be some missing features (only minor change) or
		// incompatible values (major change).

		if (version_params->common.game_version < SCS_TELEMETRY_EUT2_GAME_VERSION_1_03) { // Fixed the wheels.count attribute
			log_line(SCS_LOG_TYPE_error, "Too old version of the game");
			game_log = NULL;
			return SCS_RESULT_unsupported;
		}

		if (version_params->common.game_version < SCS_TELEMETRY_EUT2_GAME_VERSION_1_07) { // Fixed the angular acceleration calculation
			log_line(SCS_LOG_TYPE_warning, "This version of the game has less precise output of angular acceleration of the cabin");
		}

		// Future versions are fine as long the major version is not changed.

		const scs_u32_t IMPLEMENTED_VERSION = SCS_TELEMETRY_EUT2_GAME_VERSION_CURRENT;
		if (SCS_GET_MAJOR_VERSION(version_params->common.game_version) > SCS_GET_MAJOR_VERSION(IMPLEMENTED_VERSION)) {
			log_line(SCS_LOG_TYPE_warning, "Too new major version of the game, some features might behave incorrectly");
		}
	}
	else if (strcmp(version_params->common.game_id, SCS_GAME_ID_ATS) == 0) {

		// Below the minimum version there might be some missing features (only minor change) or
		// incompatible values (major change).

		const scs_u32_t MINIMAL_VERSION = SCS_TELEMETRY_ATS_GAME_VERSION_1_00;
		if (version_params->common.game_version < MINIMAL_VERSION) {
			log_line(SCS_LOG_TYPE_warning, "WARNING: Too old version of the game, some features might behave incorrectly");
		}

		// Future versions are fine as long the major version is not changed.

		const scs_u32_t IMPLEMENTED_VERSION = SCS_TELEMETRY_ATS_GAME_VERSION_CURRENT;
		if (SCS_GET_MAJOR_VERSION(version_params->common.game_version) > SCS_GET_MAJOR_VERSION(IMPLEMENTED_VERSION)) {
			log_line(SCS_LOG_TYPE_warning, "WARNING: Too new major version of the game, some features might behave incorrectly");
		}
	}
	else {
		log_line(SCS_LOG_TYPE_warning, "Unsupported game, some features or values might behave incorrectly");
	}

	// Register for events. Note that failure to register those basic events
	// likely indicates invalid usage of the api or some critical problem. As the
	// example requires all of them, we can not continue if the registration fails.

	const bool events_registered =
		(version_params->register_for_event(SCS_TELEMETRY_EVENT_paused, telemetry_pause, NULL) == SCS_RESULT_ok) &&
		(version_params->register_for_event(SCS_TELEMETRY_EVENT_started, telemetry_pause, NULL) == SCS_RESULT_ok) &&
		(version_params->register_for_event(SCS_TELEMETRY_EVENT_configuration, telemetry_configuration, NULL) == SCS_RESULT_ok) &&
		(version_params->register_for_event(SCS_TELEMETRY_EVENT_frame_start, speed_check, NULL) == SCS_RESULT_ok) 
	;
	if (! events_registered) {

		// Registrations created by unsuccessful initialization are
		// cleared automatically so we can simply exit.

		log_line(SCS_LOG_TYPE_error, "Unable to register event callbacks");
		game_log = NULL;
		return SCS_RESULT_generic_error;
	}

	// Initialize the shared memory.

	if (! initialize_shared_memory()) {
		log_line(SCS_LOG_TYPE_error, "Unable to initialize shared memory");
		game_log = NULL;
		return SCS_RESULT_generic_error;
	}

	// Register all changes we are interested in. Note that some wheel-related channels will be initialized when we
	// receive a configuration event. The channel might be missing if the game does not support it (SCS_RESULT_not_found)
	// or if does not support the requested type (SCS_RESULT_unsupported_type). For purpose of this example we ignore
	// the failures so the unsupported channels will remain at theirs default value.

#define register_channel(name, index, type, field) version_params->register_for_channel(SCS_TELEMETRY_##name, index, SCS_VALUE_TYPE_##type, SCS_TELEMETRY_CHANNEL_FLAG_no_value, telemetry_store_##type, &data.field);

	register_channel(TRUCK_CHANNEL_speed,						SCS_U32_NIL, float,			speedometer_speed);
	register_channel(TRUCK_CHANNEL_navigation_speed_limit,		SCS_U32_NIL, float,			new_max_speed);
	register_channel(TRUCK_CHANNEL_effective_throttle,			SCS_U32_NIL, float,			throttle);
	

#undef register_channel

	// Remember other the functions we will use in the future.

	register_for_channel = version_params->register_for_channel;
	unregister_from_channel = version_params->unregister_from_channel;

	// We are done.

	log_line(SCS_LOG_TYPE_message, "Memory telemetry example initialized");
	return SCS_RESULT_ok;
}

/**
 * @brief Telemetry API deinitialization function.
 *
 * See scssdk_telemetry.h
 */
SCSAPI_VOID scs_telemetry_shutdown(void)
{
	// Any cleanup needed. The registrations will be removed automatically
	// so there is no need to do that manually.

	deinitialize_shared_memory();

	unregister_from_channel = NULL;
	register_for_channel = NULL;
	game_log = NULL;
}

// Cleanup

BOOL APIENTRY DllMain(
	HMODULE module,
	DWORD  reason_for_call,
	LPVOID reseved
)
{
	return TRUE;
}

// EOF //
