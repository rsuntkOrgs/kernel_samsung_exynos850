/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  thermal.h  ($Revision: 0 $)
 *
 *  Copyright (C) 2008  Intel Corp
 *  Copyright (C) 2008  Zhang Rui <rui.zhang@intel.com>
 *  Copyright (C) 2008  Sujith Thomas <sujith.thomas@intel.com>
 */

#ifndef __THERMAL_H__
#define __THERMAL_H__

#include <linux/of.h>
#include <linux/idr.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <uapi/linux/thermal.h>

#define THERMAL_TRIPS_NONE	-1
#define THERMAL_MAX_TRIPS	16

/* invalid cooling state */
#define THERMAL_CSTATE_INVALID -1UL

/* invalid cooling frequency */
#define THERMAL_CFREQ_INVALID -1

/* No upper/lower limit requirement */
#define THERMAL_NO_LIMIT	((u32)~0)

/* Default weight of a bound cooling device */
#define THERMAL_WEIGHT_DEFAULT 0

/* Max sensors that can be used for a single virtual thermalzone */
#define THERMAL_MAX_VIRT_SENSORS 10

/* use value, which < 0K, to indicate an invalid/uninitialized temperature */
#define THERMAL_TEMP_INVALID	-274000

/* Unit conversion macros */
#define DECI_KELVIN_TO_CELSIUS(t)	({			\
	long _t = (t);						\
	((_t-2732 >= 0) ? (_t-2732+5)/10 : (_t-2732-5)/10);	\
})
#define CELSIUS_TO_DECI_KELVIN(t)	((t)*10+2732)
#define DECI_KELVIN_TO_MILLICELSIUS_WITH_OFFSET(t, off) (((t) - (off)) * 100)
#define DECI_KELVIN_TO_MILLICELSIUS(t) DECI_KELVIN_TO_MILLICELSIUS_WITH_OFFSET(t, 2732)
#define MILLICELSIUS_TO_DECI_KELVIN_WITH_OFFSET(t, off) (((t) / 100) + (off))
#define MILLICELSIUS_TO_DECI_KELVIN(t) MILLICELSIUS_TO_DECI_KELVIN_WITH_OFFSET(t, 2732)

/* Default Thermal Governor */
#if defined(CONFIG_THERMAL_DEFAULT_GOV_STEP_WISE)
#define DEFAULT_THERMAL_GOVERNOR       "step_wise"
#elif defined(CONFIG_THERMAL_DEFAULT_GOV_FAIR_SHARE)
#define DEFAULT_THERMAL_GOVERNOR       "fair_share"
#elif defined(CONFIG_THERMAL_DEFAULT_GOV_USER_SPACE)
#define DEFAULT_THERMAL_GOVERNOR       "user_space"
#elif defined(CONFIG_THERMAL_DEFAULT_GOV_POWER_ALLOCATOR)
#define DEFAULT_THERMAL_GOVERNOR       "power_allocator"
#endif

struct thermal_zone_device;
struct thermal_cooling_device;
struct thermal_instance;

enum thermal_device_mode {
	THERMAL_DEVICE_DISABLED = 0,
	THERMAL_DEVICE_ENABLED,
};

enum thermal_trip_type {
	THERMAL_TRIP_ACTIVE = 0,
	THERMAL_TRIP_PASSIVE,
	THERMAL_TRIP_HOT,
	THERMAL_TRIP_CRITICAL,
};

enum thermal_trend {
	THERMAL_TREND_STABLE, /* temperature is stable */
	THERMAL_TREND_RAISING, /* temperature is raising */
	THERMAL_TREND_DROPPING, /* temperature is dropping */
	THERMAL_TREND_RAISE_FULL, /* apply highest cooling action */
	THERMAL_TREND_DROP_FULL, /* apply lowest cooling action */
};

/* Thermal notification reason */
enum thermal_notify_event {
	THERMAL_EVENT_UNSPECIFIED, /* Unspecified event */
	THERMAL_EVENT_TEMP_SAMPLE, /* New Temperature sample */
	THERMAL_TRIP_VIOLATED, /* TRIP Point violation */
	THERMAL_TRIP_CHANGED, /* TRIP Point temperature changed */
	THERMAL_DEVICE_DOWN, /* Thermal device is down */
	THERMAL_DEVICE_UP, /* Thermal device is up after a down event */
	THERMAL_DEVICE_POWER_CAPABILITY_CHANGED, /* power capability changed */
	THERMAL_TABLE_CHANGED, /* Thermal table(s) changed */
};

struct thermal_zone_device_ops {
	int (*bind) (struct thermal_zone_device *,
		     struct thermal_cooling_device *);
	int (*unbind) (struct thermal_zone_device *,
		       struct thermal_cooling_device *);
	int (*get_temp) (struct thermal_zone_device *, int *);
	int (*set_trips) (struct thermal_zone_device *, int, int);
	int (*get_mode) (struct thermal_zone_device *,
			 enum thermal_device_mode *);
	int (*set_mode) (struct thermal_zone_device *,
		enum thermal_device_mode);
	int (*get_trip_type) (struct thermal_zone_device *, int,
		enum thermal_trip_type *);
	int (*get_trip_temp) (struct thermal_zone_device *, int, int *);
	int (*set_trip_temp) (struct thermal_zone_device *, int, int);
	int (*get_trip_hyst) (struct thermal_zone_device *, int, int *);
	int (*set_trip_hyst) (struct thermal_zone_device *, int, int);
	int (*get_crit_temp) (struct thermal_zone_device *, int *);
	int (*set_emul_temp) (struct thermal_zone_device *, int);
	int (*get_trend) (struct thermal_zone_device *, int,
			  enum thermal_trend *);
	int (*notify) (struct thermal_zone_device *, int,
		       enum thermal_trip_type);
	int (*throttle_hotplug) (struct thermal_zone_device *);
};

struct thermal_cooling_device_ops {
	int (*get_max_state) (struct thermal_cooling_device *, unsigned long *);
	int (*get_cur_state) (struct thermal_cooling_device *, unsigned long *);
	int (*set_cur_state) (struct thermal_cooling_device *, unsigned long);
	int (*get_requested_power)(struct thermal_cooling_device *,
				   struct thermal_zone_device *, u32 *);
	int (*state2power)(struct thermal_cooling_device *,
			   struct thermal_zone_device *, unsigned long, u32 *);
	int (*power2state)(struct thermal_cooling_device *,
			   struct thermal_zone_device *, u32, unsigned long *);
	int (*set_cur_temp)(struct thermal_cooling_device *, bool, int);
	int (*get_cooling_level)(struct thermal_cooling_device *, unsigned long);
};

struct thermal_cooling_device {
	int id;
	char type[THERMAL_NAME_LENGTH];
	struct device device;
	struct device_node *np;
	void *devdata;
	void *stats;
	const struct thermal_cooling_device_ops *ops;
	bool updated; /* true if the cooling device does not need update */
	struct mutex lock; /* protect thermal_instances list */
	struct list_head thermal_instances;
	struct list_head node;
	unsigned long sysfs_cur_state_req;
	unsigned long sysfs_min_state_req;
};

struct thermal_attr {
	struct device_attribute attr;
	char name[THERMAL_NAME_LENGTH];
};

/**
 * struct thermal_zone_device - structure for a thermal zone
 * @id:		unique id number for each thermal zone
 * @type:	the thermal zone device type
 * @device:	&struct device for this thermal zone
 * @trip_temp_attrs:	attributes for trip points for sysfs: trip temperature
 * @trip_type_attrs:	attributes for trip points for sysfs: trip type
 * @trip_hyst_attrs:	attributes for trip points for sysfs: trip hysteresis
 * @devdata:	private pointer for device private data
 * @trips:	number of trip points the thermal zone supports
 * @trips_disabled;	bitmap for disabled trips
 * @passive_delay:	number of milliseconds to wait between polls when
 *			performing passive cooling.
 * @polling_delay:	number of milliseconds to wait between polls when
 *			checking whether trip points have been crossed (0 for
 *			interrupt driven systems)
 * @temperature:	current temperature.  This is only for core code,
 *			drivers should use thermal_zone_get_temp() to get the
 *			current temperature
 * @last_temperature:	previous temperature read
 * @emul_temperature:	emulated temperature when using CONFIG_THERMAL_EMULATION
 * @passive:		1 if you've crossed a passive trip point, 0 otherwise.
 * @prev_low_trip:	the low current temperature if you've crossed a passive
			trip point.
 * @prev_high_trip:	the above current temperature if you've crossed a
			passive trip point.
 * @forced_passive:	If > 0, temperature at which to switch on all ACPI
 *			processor cooling devices.  Currently only used by the
 *			step-wise governor.
 * @need_update:	if equals 1, thermal_zone_device_update needs to be invoked.
 * @ops:	operations this &thermal_zone_device supports
 * @tzp:	thermal zone parameters
 * @governor:	pointer to the governor for this thermal zone
 * @governor_data:	private pointer for governor data
 * @thermal_instances:	list of &struct thermal_instance of this thermal zone
 * @ida:	&struct ida to generate unique id for this zone's cooling
 *		devices
 * @lock:	lock to protect thermal_instances list
 * @node:	node in thermal_tz_list (in thermal_core.c)
 * @poll_queue:	delayed work for polling
 * @notify_event: Last notification event
 * @cdev_bound: cooling device bind done
 */
struct thermal_zone_device {
	int id;
	char type[THERMAL_NAME_LENGTH];
	struct device device;
	struct attribute_group trips_attribute_group;
	struct thermal_attr *trip_temp_attrs;
	struct thermal_attr *trip_type_attrs;
	struct thermal_attr *trip_hyst_attrs;
	void *devdata;
	int trips;
	unsigned long trips_disabled;	/* bitmap for disabled trips */
	int passive_delay;
	int polling_delay;
	int temperature;
	int last_temperature;
	int emul_temperature;
	int passive;
	int prev_low_trip;
	int prev_high_trip;
	unsigned int forced_passive;
	atomic_t need_update;
	struct thermal_zone_device_ops *ops;
	struct thermal_zone_params *tzp;
	struct thermal_governor *governor;
	void *governor_data;
	struct list_head thermal_instances;
	struct ida ida;
	struct mutex lock;
	struct list_head node;
	struct delayed_work poll_queue;
	enum thermal_notify_event notify_event;
	bool cdev_bound;
};

/**
 * struct thermal_governor - structure that holds thermal governor information
 * @name:	name of the governor
 * @bind_to_tz: callback called when binding to a thermal zone.  If it
 *		returns 0, the governor is bound to the thermal zone,
 *		otherwise it fails.
 * @unbind_from_tz:	callback called when a governor is unbound from a
 *			thermal zone.
 * @throttle:	callback called for every trip point even if temperature is
 *		below the trip point temperature
 * @governor_list:	node in thermal_governor_list (in thermal_core.c)
 */
struct thermal_governor {
	char name[THERMAL_NAME_LENGTH];
	int (*bind_to_tz)(struct thermal_zone_device *tz);
	void (*unbind_from_tz)(struct thermal_zone_device *tz);
	int (*throttle)(struct thermal_zone_device *tz, int trip);
	struct list_head	governor_list;
};

/* Structure that holds binding parameters for a zone */
struct thermal_bind_params {
	struct thermal_cooling_device *cdev;

	/*
	 * This is a measure of 'how effectively these devices can
	 * cool 'this' thermal zone. It shall be determined by
	 * platform characterization. This value is relative to the
	 * rest of the weights so a cooling device whose weight is
	 * double that of another cooling device is twice as
	 * effective. See Documentation/thermal/sysfs-api.txt for more
	 * information.
	 */
	int weight;

	/*
	 * This is a bit mask that gives the binding relation between this
	 * thermal zone and cdev, for a particular trip point.
	 * See Documentation/thermal/sysfs-api.txt for more information.
	 */
	int trip_mask;

	/*
	 * This is an array of cooling state limits. Must have exactly
	 * 2 * thermal_zone.number_of_trip_points. It is an array consisting
	 * of tuples <lower-state upper-state> of state limits. Each trip
	 * will be associated with one state limit tuple when binding.
	 * A NULL pointer means <THERMAL_NO_LIMITS THERMAL_NO_LIMITS>
	 * on all trips.
	 */
	unsigned long *binding_limits;
	int (*match) (struct thermal_zone_device *tz,
			struct thermal_cooling_device *cdev);
};

/* Structure to define Thermal Zone parameters */
struct thermal_zone_params {
	char governor_name[THERMAL_NAME_LENGTH];

	/*
	 * a boolean to indicate if the thermal to hwmon sysfs interface
	 * is required. when no_hwmon == false, a hwmon sysfs interface
	 * will be created. when no_hwmon == true, nothing will be done
	 */
	bool no_hwmon;

	int num_tbps;	/* Number of tbp entries */
	struct thermal_bind_params *tbp;

	/*
	 * Sustainable power (heat) that this thermal zone can dissipate in
	 * mW
	 */
	u32 sustainable_power;

	/*
	 * Proportional parameter of the PID controller when
	 * overshooting (i.e., when temperature is below the target)
	 */
	s32 k_po;

	/*
	 * Proportional parameter of the PID controller when
	 * undershooting
	 */
	s32 k_pu;

	/* Integral parameter of the PID controller */
	s32 k_i;

	/* Derivative parameter of the PID controller */
	s32 k_d;

	/* threshold below which the error is no longer accumulated */
	s32 integral_cutoff;

	s32 integral_max;

	/*
	 * @slope:	slope of a linear temperature adjustment curve.
	 * 		Used by thermal zone drivers.
	 */
	int slope;
	/*
	 * @offset:	offset of a linear temperature adjustment curve.
	 * 		Used by thermal zone drivers (default 0).
	 */
	int offset;

	/*
	 * @tracks_low:	Indicates that the thermal zone params are for
	 *		temperatures falling below the thresholds.
	 */
	bool tracks_low;
};

struct thermal_genl_event {
	u32 orig;
	enum events event;
};

/**
 * struct thermal_zone_of_device_ops - scallbacks for handling DT based zones
 *
 * Mandatory:
 * @get_temp: a pointer to a function that reads the sensor temperature.
 *
 * Optional:
 * @get_trend: a pointer to a function that reads the sensor temperature trend.
 * @set_trips: a pointer to a function that sets a temperature window. When
 *	       this window is left the driver must inform the thermal core via
 *	       thermal_zone_device_update.
 * @set_emul_temp: a pointer to a function that sets sensor emulated
 *		   temperature.
 * @set_trip_temp: a pointer to a function that sets the trip temperature on
 *		   hardware.
 */
struct thermal_zone_of_device_ops {
	int (*get_temp)(void *, int *);
	int (*get_trend)(void *, int, enum thermal_trend *);
	int (*set_trips)(void *, int, int);
	int (*set_emul_temp)(void *, int);
	int (*set_trip_temp)(void *, int, int);
	int (*throttle_cpu_hotplug)(void *, int temp);
};

/**
 * struct thermal_trip - representation of a point in temperature domain
 * @np: pointer to struct device_node that this trip point was created from
 * @temperature: temperature value in miliCelsius
 * @hysteresis: relative hysteresis in miliCelsius
 * @type: trip point type
 */

struct thermal_trip {
	struct device_node *np;
	int temperature;
	int hysteresis;
	enum thermal_trip_type type;
};

/***   Private data structures to represent thermal device tree data ***/

/**
 * struct __thermal_bind_param - a match between trip and cooling device
 * @cooling_device: a pointer to identify the referred cooling device
 * @trip_id: the trip point index
 * @usage: the percentage (from 0 to 100) of cooling contribution
 * @min: minimum cooling state used at this trip point
 * @max: maximum cooling state used at this trip point
 * @value: cooling value corresponding to max state
 */

struct __thermal_bind_params {
	struct device_node *cooling_device;
	unsigned int trip_id;
	unsigned int usage;
	unsigned long min;
	unsigned long max;
	unsigned long value;
};

/**
 * struct __thermal_zone - internal representation of a thermal zone
 * @mode: current thermal zone device mode (enabled/disabled)
 * @passive_delay: polling interval while passive cooling is activated
 * @polling_delay: zone polling interval
 * @slope: slope of the temperature adjustment curve
 * @offset: offset of the temperature adjustment curve
 * @ntrips: number of trip points
 * @trips: an array of trip points (0..ntrips - 1)
 * @num_tbps: number of thermal bind params
 * @tbps: an array of thermal bind params (0..num_tbps - 1)
 * @sensor_data: sensor private data used while reading temperature and trend
 * @ops: set of callbacks to handle the thermal zone based on DT
 */

struct __thermal_zone {
	enum thermal_device_mode mode;
	int passive_delay;
	int polling_delay;
	int slope;
	int offset;
	struct thermal_zone_device *tzd;
	bool default_disable;

	/* trip data */
	int ntrips;
	struct thermal_trip *trips;

	/* cooling binding data */
	int num_tbps;
	struct __thermal_bind_params *tbps;

	const struct thermal_zone_of_device_ops *ops;
	struct list_head list;
	/* sensor interface */
	struct __sensor_param *senps;
};

/* Different aggregation logic supported for virtual sensors */
enum aggregation_logic {
	VIRT_WEIGHTED_AVG,
	VIRT_MAXIMUM,
	VIRT_MINIMUM,
	VIRT_COUNT_THRESHOLD,
	VIRT_AGGREGATION_NR,
};

/*
 * struct virtual_sensor_data - Data structure used to provide
 *			      information about the virtual zone.
 * @virt_zone_name - Virtual thermal zone name
 * @num_sensors - Number of sensors this virtual zone uses to compute
 *		  temperature
 * @sensor_names - Array of sensor names
 * @logic - Temperature aggregation logic to be used
 * @coefficients - Coefficients to be used for weighted average logic
 * @coefficient_ct - number of coefficients provided as input
 * @avg_offset - offset value to be used for the weighted aggregation logic
 * @avg_denominator - denominator value to be used for the weighted aggregation
 *			logic
 */
struct virtual_sensor_data {
	int                    num_sensors;
	char                   virt_zone_name[THERMAL_NAME_LENGTH];
	char                   *sensor_names[THERMAL_MAX_VIRT_SENSORS];
	enum aggregation_logic logic;
	int                    coefficients[THERMAL_MAX_VIRT_SENSORS];
	int                    coefficient_ct;
	int                    avg_offset;
	int                    avg_denominator;
};

/* Function declarations */
#ifdef CONFIG_THERMAL_OF
struct thermal_zone_device *
thermal_zone_of_sensor_register(struct device *dev, int id, void *data,
				const struct thermal_zone_of_device_ops *ops);
void thermal_zone_of_sensor_unregister(struct device *dev,
				       struct thermal_zone_device *tz);
struct thermal_zone_device *devm_thermal_zone_of_sensor_register(
		struct device *dev, int id, void *data,
		const struct thermal_zone_of_device_ops *ops);
void devm_thermal_zone_of_sensor_unregister(struct device *dev,
					    struct thermal_zone_device *tz);
struct thermal_zone_device *devm_thermal_of_virtual_sensor_register(
		struct device *dev,
		const struct virtual_sensor_data *sensor_data);
#else
static inline struct thermal_zone_device *
thermal_zone_of_sensor_register(struct device *dev, int id, void *data,
				const struct thermal_zone_of_device_ops *ops)
{
	return ERR_PTR(-ENODEV);
}

static inline
void thermal_zone_of_sensor_unregister(struct device *dev,
				       struct thermal_zone_device *tz)
{
}

static inline struct thermal_zone_device *devm_thermal_zone_of_sensor_register(
		struct device *dev, int id, void *data,
		const struct thermal_zone_of_device_ops *ops)
{
	return ERR_PTR(-ENODEV);
}

static inline
void devm_thermal_zone_of_sensor_unregister(struct device *dev,
					    struct thermal_zone_device *tz)
{
}

static inline
struct thermal_zone_device *devm_thermal_of_virtual_sensor_register(
		struct device *dev,
		const struct virtual_sensor_data *sensor_data)
{
	return ERR_PTR(-ENODEV);
}

#endif

#if IS_ENABLED(CONFIG_THERMAL)
static inline bool cdev_is_power_actor(struct thermal_cooling_device *cdev)
{
	return cdev->ops->get_requested_power && cdev->ops->state2power &&
		cdev->ops->power2state;
}

int power_actor_get_max_power(struct thermal_cooling_device *,
			      struct thermal_zone_device *tz, u32 *max_power);
int power_actor_get_min_power(struct thermal_cooling_device *,
			      struct thermal_zone_device *tz, u32 *min_power);
int power_actor_set_power(struct thermal_cooling_device *,
			  struct thermal_instance *, u32);
struct thermal_zone_device *thermal_zone_device_register(const char *, int, int,
		void *, struct thermal_zone_device_ops *,
		struct thermal_zone_params *, int, int);
void thermal_zone_device_unregister(struct thermal_zone_device *);

int thermal_zone_bind_cooling_device(struct thermal_zone_device *, int,
				     struct thermal_cooling_device *,
				     unsigned long, unsigned long,
				     unsigned int);
int thermal_zone_unbind_cooling_device(struct thermal_zone_device *, int,
				       struct thermal_cooling_device *);
void thermal_zone_device_update(struct thermal_zone_device *,
				enum thermal_notify_event);
void thermal_zone_set_trips(struct thermal_zone_device *);

struct thermal_cooling_device *thermal_cooling_device_register(char *, void *,
		const struct thermal_cooling_device_ops *);
struct thermal_cooling_device *
thermal_of_cooling_device_register(struct device_node *np, char *, void *,
				   const struct thermal_cooling_device_ops *);
void thermal_cooling_device_unregister(struct thermal_cooling_device *);
struct thermal_zone_device *thermal_zone_get_zone_by_name(const char *name);
struct thermal_zone_device *thermal_zone_get_zone_by_cool_np(struct device_node *cool_np);
int thermal_zone_get_temp(struct thermal_zone_device *tz, int *temp);
int thermal_zone_get_slope(struct thermal_zone_device *tz);
int thermal_zone_get_offset(struct thermal_zone_device *tz);

int get_tz_trend(struct thermal_zone_device *, int);
struct thermal_instance *get_thermal_instance(struct thermal_zone_device *,
		struct thermal_cooling_device *, int);
void thermal_cdev_update(struct thermal_cooling_device *);
void thermal_notify_framework(struct thermal_zone_device *, int);
#else
static inline bool cdev_is_power_actor(struct thermal_cooling_device *cdev)
{ return false; }
static inline int power_actor_get_max_power(struct thermal_cooling_device *cdev,
			      struct thermal_zone_device *tz, u32 *max_power)
{ return 0; }
static inline int power_actor_get_min_power(struct thermal_cooling_device *cdev,
					    struct thermal_zone_device *tz,
					    u32 *min_power)
{ return -ENODEV; }
static inline int power_actor_set_power(struct thermal_cooling_device *cdev,
			  struct thermal_instance *tz, u32 power)
{ return 0; }
static inline struct thermal_zone_device *thermal_zone_device_register(
	const char *type, int trips, int mask, void *devdata,
	struct thermal_zone_device_ops *ops,
	struct thermal_zone_params *tzp,
	int passive_delay, int polling_delay)
{ return ERR_PTR(-ENODEV); }
static inline void thermal_zone_device_unregister(
	struct thermal_zone_device *tz)
{ }
static inline int thermal_zone_bind_cooling_device(
	struct thermal_zone_device *tz, int trip,
	struct thermal_cooling_device *cdev,
	unsigned long upper, unsigned long lower,
	unsigned int weight)
{ return -ENODEV; }
static inline int thermal_zone_unbind_cooling_device(
	struct thermal_zone_device *tz, int trip,
	struct thermal_cooling_device *cdev)
{ return -ENODEV; }
static inline void thermal_zone_device_update(struct thermal_zone_device *tz,
					      enum thermal_notify_event event)
{ }
static inline void thermal_zone_set_trips(struct thermal_zone_device *tz)
{ }
static inline struct thermal_cooling_device *
thermal_cooling_device_register(char *type, void *devdata,
	const struct thermal_cooling_device_ops *ops)
{ return ERR_PTR(-ENODEV); }
static inline struct thermal_cooling_device *
thermal_of_cooling_device_register(struct device_node *np,
	char *type, void *devdata, const struct thermal_cooling_device_ops *ops)
{ return ERR_PTR(-ENODEV); }
static inline void thermal_cooling_device_unregister(
	struct thermal_cooling_device *cdev)
{ }
static inline struct thermal_zone_device *thermal_zone_get_zone_by_name(
		const char *name)
{ return ERR_PTR(-ENODEV); }
static inline struct thermal_zone_device *thermal_zone_get_zone_by_cool_np(
		struct device_node *cool_np)
{ return ERR_PTR(-ENODEV); }
static inline int thermal_zone_get_temp(
		struct thermal_zone_device *tz, int *temp)
{ return -ENODEV; }
static inline int thermal_zone_get_slope(
		struct thermal_zone_device *tz)
{ return -ENODEV; }
static inline int thermal_zone_get_offset(
		struct thermal_zone_device *tz)
{ return -ENODEV; }
static inline int get_tz_trend(struct thermal_zone_device *tz, int trip)
{ return -ENODEV; }
static inline struct thermal_instance *
get_thermal_instance(struct thermal_zone_device *tz,
	struct thermal_cooling_device *cdev, int trip)
{ return ERR_PTR(-ENODEV); }
static inline void thermal_cdev_update(struct thermal_cooling_device *cdev)
{ }
static inline void thermal_notify_framework(struct thermal_zone_device *tz,
	int trip)
{ }
#endif /* CONFIG_THERMAL */

#if defined(CONFIG_NET) && IS_ENABLED(CONFIG_THERMAL)
extern int thermal_generate_netlink_event(struct thermal_zone_device *tz,
						enum events event);
#else
static inline int thermal_generate_netlink_event(struct thermal_zone_device *tz,
						enum events event)
{
	return 0;
}
#endif

#endif /* __THERMAL_H__ */
