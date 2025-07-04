/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <AP_HAL/AP_HAL.h>
#include <AP_Math/AP_Math.h>
#include <SRV_Channel/SRV_Channel.h>
#include <GCS_MAVLink/GCS.h>
#include "AP_MotorsUGV.h"
#include <AP_Relay/AP_Relay.h>

#define SERVO_MAX 4500  // This value represents 45 degrees and is just an arbitrary representation of servo max travel.

extern const AP_HAL::HAL& hal;

// singleton instance
AP_MotorsUGV *AP_MotorsUGV::_singleton;

// parameters for the motor class
const AP_Param::GroupInfo AP_MotorsUGV::var_info[] = {
    // @Param: PWM_TYPE
    // @DisplayName: Motor Output PWM type
    // @Description: This selects the output PWM type as regular PWM, OneShot, Brushed motor support using PWM (duty cycle) with separated direction signal, Brushed motor support with separate throttle and direction PWM (duty cycle)
    // @Values: 0:Normal,1:OneShot,2:OneShot125,3:BrushedWithRelay,4:BrushedBiPolar,5:DShot150,6:DShot300,7:DShot600,8:DShot1200
    // @User: Advanced
    // @RebootRequired: True
    AP_GROUPINFO("PWM_TYPE", 1, AP_MotorsUGV, _pwm_type, PWMType::NORMAL),

    // @Param: PWM_FREQ
    // @DisplayName: Motor Output PWM freq for brushed motors
    // @Description: Motor Output PWM freq for brushed motors
    // @Units: kHz
    // @Range: 1 20
    // @Increment: 1
    // @User: Advanced
    // @RebootRequired: True
    AP_GROUPINFO("PWM_FREQ", 2, AP_MotorsUGV, _pwm_freq, 16),

    // @Param: SAFE_DISARM
    // @DisplayName: Motor PWM output disabled when disarmed
    // @Description: Disables motor PWM output when disarmed
    // @Values: 0:PWM enabled while disarmed, 1:PWM disabled while disarmed
    // @User: Advanced
    AP_GROUPINFO("SAFE_DISARM", 3, AP_MotorsUGV, _disarm_disable_pwm, 0),

    // @Param: THR_MIN
    // @DisplayName: Throttle minimum
    // @Description: Throttle minimum percentage the autopilot will apply. This is useful for handling a deadzone around low throttle and for preventing internal combustion motors cutting out during missions.
    // @Units: %
    // @Range: 0 20
    // @Increment: 1
    // @User: Standard
    AP_GROUPINFO("THR_MIN", 5, AP_MotorsUGV, _throttle_min, 0),

    // @Param: THR_MAX
    // @DisplayName: Throttle maximum
    // @Description: Throttle maximum percentage the autopilot will apply. This can be used to prevent overheating an ESC or motor on an electric rover
    // @Units: %
    // @Range: 30 100
    // @Increment: 1
    // @User: Standard
    AP_GROUPINFO("THR_MAX", 6, AP_MotorsUGV, _throttle_max, 100),

    // @Param: SLEWRATE
    // @DisplayName: Throttle slew rate
    // @Description: Throttle slew rate as a percentage of total range per second. A value of 100 allows the motor to change over its full range in one second.  A value of zero disables the limit.  Note some NiMH powered rovers require a lower setting of 40 to reduce current demand to avoid brownouts.
    // @Units: %/s
    // @Range: 0 1000
    // @Increment: 1
    // @User: Standard
    AP_GROUPINFO("SLEWRATE", 8, AP_MotorsUGV, _slew_rate, 100),

    // @Param: THST_EXPO
    // @DisplayName: Thrust Curve Expo
    // @Description: Thrust curve exponent (-1 to +1 with 0 being linear)
    // @Range: -1.0 1.0
    // @User: Advanced
    AP_GROUPINFO("THST_EXPO", 9, AP_MotorsUGV, _thrust_curve_expo, 0.0f),

    // 10 was VEC_THR_BASE

    // @Param: SPD_SCA_BASE
    // @DisplayName: Motor speed scaling base speed
    // @Description: Speed above which steering is scaled down when using regular steering/throttle vehicles.  zero to disable speed scaling
    // @Units: m/s
    // @Range: 0 10
    // @User: Advanced
    AP_GROUPINFO("SPD_SCA_BASE", 11, AP_MotorsUGV, _speed_scale_base, 1.0f),

    // @Param: STR_THR_MIX
    // @DisplayName: Motor steering vs throttle prioritisation
    // @Description: Steering vs Throttle priorisation.  Higher numbers prioritise steering, lower numbers prioritise throttle.  Only valid for Skid Steering vehicles
    // @Range: 0.2 1.0
    // @User: Advanced
    AP_GROUPINFO("STR_THR_MIX", 12, AP_MotorsUGV, _steering_throttle_mix, 0.5f),

    // @Param: VEC_ANGLEMAX
    // @DisplayName: Vector thrust angle max
    // @Description: The angle between steering's middle position and maximum position when using vectored thrust (boats only)
    // @Units: deg
    // @Range: 0 90
    // @User: Standard
    AP_GROUPINFO("VEC_ANGLEMAX", 13, AP_MotorsUGV, _vector_angle_max, 0.0f),

    // @Param: THST_ASYM
    // @DisplayName: Motor Thrust Asymmetry
    // @Description: Thrust Asymmetry. Used for skid-steering. 2.0 means your motors move twice as fast forward than they do backwards.
    // @Range: 1.0 10.0
    // @User: Advanced
    AP_GROUPINFO("THST_ASYM", 14, AP_MotorsUGV, _thrust_asymmetry, 1.0f),

    // @Param: REV_DELAY
    // @DisplayName: Motor reversal delay
    // @Description: For reversible motors that need a delay before they can change direction. When greater than zero the throttle will go to zero for this amount of time before outputting the new throttle when the demanded motor direction changes.
    // @Units: s
    // @Range: 0.1 1.0
    // @Increment: 0.1
    // @User: Standard
    AP_GROUPINFO("REV_DELAY", 15, AP_MotorsUGV, _reverse_delay, 0),
    
    AP_GROUPEND
};

AP_MotorsUGV::AP_MotorsUGV(AP_WheelRateControl& rate_controller, AP_StepperController& stepper_ctrl) :
    _rate_controller(rate_controller),
    _stepper_ctrl(stepper_ctrl)
{
    AP_Param::setup_object_defaults(this, var_info);
    _singleton = this;
}

void AP_MotorsUGV::init(uint8_t frtype)
{
    _frame_type = frame_type(frtype);

    // setup for omni vehicles
    if (_frame_type != FRAME_TYPE_UNDEFINED) {
        setup_omni();
    }
    
    // setup servo output
    setup_servo_output();

    // setup pwm type
    setup_pwm_type();

    setup_stepper_ctrl();

    // set safety output
    setup_safety_output();
    
}

bool AP_MotorsUGV::get_legacy_relay_index(int8_t &index1, int8_t &index2, int8_t &index3, int8_t &index4) const
{
    if (_pwm_type != PWMType::BRUSHED_WITH_RELAY) {
        // Relays only used if PWM type is set to brushed with relay
        return false;
    }

    // First relay is always used, throttle, throttle left or motor 1
    index1 = 0;

    // Second relay is used for right throttle on skid steer and motor 2 for omni
    if (have_skid_steering()) {
        index2 = 1;
    }

    // Omni can have a variable number of motors
    if (is_omni()) {
        // Omni has at least 3 motors
        index2 = 2;
        if (_motors_num >= 4) {
            index2 = 3;
        }
    }

    return true;
}

// setup output in case of main CPU failure
void AP_MotorsUGV::setup_safety_output()
{
    if (_pwm_type == PWMType::BRUSHED_WITH_RELAY) {
        // set trim to min to set duty cycle range (0 - 100%) to servo range
        // ignore servo revese flag, it is used by the relay
        SRV_Channels::set_trim_to_min_for(SRV_Channel::k_throttle, true);
        SRV_Channels::set_trim_to_min_for(SRV_Channel::k_throttleLeft, true);
        SRV_Channels::set_trim_to_min_for(SRV_Channel::k_throttleRight, true);
    }

    // stop sending pwm if main CPU fails
    SRV_Channels::set_failsafe_limit(SRV_Channel::k_throttle, SRV_Channel::Limit::ZERO_PWM);
    SRV_Channels::set_failsafe_limit(SRV_Channel::k_throttleLeft, SRV_Channel::Limit::ZERO_PWM);
    SRV_Channels::set_failsafe_limit(SRV_Channel::k_throttleRight, SRV_Channel::Limit::ZERO_PWM);
}

// setup servo output ranges
void AP_MotorsUGV::setup_servo_output()
{
    // k_steering are limited to -45;45 degree
    SRV_Channels::set_angle(SRV_Channel::k_steering, SERVO_MAX);

    // k_throttle are in power percent so -100 ... 100
    SRV_Channels::set_angle(SRV_Channel::k_throttle, 100);

    // skid steering left/right throttle as -1000 to 1000 values
    SRV_Channels::set_angle(SRV_Channel::k_throttleLeft,  1000);
    SRV_Channels::set_angle(SRV_Channel::k_throttleRight, 1000);

    // omni motors set in power percent so -100 ... 100
    for (uint8_t i=0; i<AP_MOTORS_NUM_MOTORS_MAX; i++) {
        SRV_Channel::Function function = SRV_Channels::get_motor_function(i);
        SRV_Channels::set_angle(function, 100);
    }

    // mainsail range from 0 to 100
    SRV_Channels::set_range(SRV_Channel::k_mainsail_sheet, 100);
    // wing sail -100 to 100
    SRV_Channels::set_angle(SRV_Channel::k_wingsail_elevator, 100);
    // mast rotation -100 to 100
    SRV_Channels::set_angle(SRV_Channel::k_mast_rotation, 100);

}

// set steering as a value from -4500 to +4500
//   apply_scaling should be set to false for manual modes where
//   no scaling by speed or angle should be performed
void AP_MotorsUGV::set_steering(float steering, bool apply_scaling)
{
    _steering = steering;
    _scale_steering = apply_scaling;
}

// set throttle as a value from -100 to 100
void AP_MotorsUGV::set_throttle(float throttle)
{
    // only allow setting throttle if armed
    if (!hal.util->get_soft_armed()) {
        return;
    }

    // check throttle is between -_throttle_max and  +_throttle_max
    _throttle = constrain_float(throttle, -_throttle_max, _throttle_max);
}

// set lateral input as a value from -100 to +100
void AP_MotorsUGV::set_lateral(float lateral)
{
    _lateral = constrain_float(lateral, -100.0f, 100.0f);
}

// set roll input as a value from -1 to +1
void AP_MotorsUGV::set_roll(float roll)
{
    _roll = constrain_float(roll, -1.0f, 1.0f);
}

// set pitch input as a value from -1 to +1
void AP_MotorsUGV::set_pitch(float pitch)
{
    _pitch = constrain_float(pitch, -1.0f, 1.0f);
}

// set walking_height input as a value from -1 to +1
void AP_MotorsUGV::set_walking_height(float walking_height)
{
    _walking_height = constrain_float(walking_height, -1.0f, 1.0f);
}

// set mainsail input as a value from 0 to 100
void AP_MotorsUGV::set_mainsail(float mainsail)
{
    _mainsail = constrain_float(mainsail, 0.0f, 100.0f);
}

// set wingsail input as a value from -100 to 100
void AP_MotorsUGV::set_wingsail(float wingsail)
{
    _wingsail = constrain_float(wingsail, -100.0f, 100.0f);
}

// set mast rotation input as a value from -100 to 100
void AP_MotorsUGV::set_mast_rotation(float mast_rotation)
{
    _mast_rotation = constrain_float(mast_rotation, -100.0f, 100.0f);
}

// get slew limited throttle
// used by manual mode to avoid bad steering behaviour during transitions from forward to reverse
// same as private slew_limit_throttle method (see below) but does not update throttle state
float AP_MotorsUGV::get_slew_limited_throttle(float throttle, float dt) const
{
    if (_slew_rate <= 0) {
        return throttle;
    }

    const float throttle_change_max = static_cast<float>(_slew_rate) * dt;
    return constrain_float(throttle, _throttle_prev - throttle_change_max, _throttle_prev + throttle_change_max);
}

/*
  work out if skid steering is available
 */
bool AP_MotorsUGV::have_skid_steering() const
{
    return (SRV_Channels::function_assigned(SRV_Channel::k_throttleLeft) && SRV_Channels::function_assigned(SRV_Channel::k_throttleRight)) || is_omni();
}

// true if the vehicle has a mainsail
bool AP_MotorsUGV::has_sail() const
{
    return SRV_Channels::function_assigned(SRV_Channel::k_mainsail_sheet) || SRV_Channels::function_assigned(SRV_Channel::k_wingsail_elevator) || SRV_Channels::function_assigned(SRV_Channel::k_mast_rotation);
}

void AP_MotorsUGV::output(bool armed, float ground_speed, float dt)
{
    // soft-armed overrides passed in armed status
    if (!hal.util->get_soft_armed()) {
        armed = false;
        _throttle = 0.0f;
    }

    // clear limit flags
    // output_ methods are responsible for setting them to true if required on each iteration
    limit.steer_left = limit.steer_right = limit.throttle_lower = limit.throttle_upper = false;

    // sanity check parameters
    sanity_check_parameters();

    // slew limit throttle
    slew_limit_throttle(dt);

    // output for regular steering/throttle style frames
    output_regular(armed, ground_speed, _steering, _throttle);

    // output for skid steering style frames
    output_skid_steering(armed, _steering, _throttle, dt);

    // output for omni frames
    output_omni(armed, _steering, _throttle, _lateral);

    // output to sails
    output_sail();

    // send values to the PWM timers for output
    auto &srv = AP::srv();
    SRV_Channels::calc_pwm();
    srv.cork();
    SRV_Channels::output_ch_all();
    srv.push();
}

// test steering or throttle output as a percentage of the total (range -100 to +100)
// used in response to DO_MOTOR_TEST mavlink command
bool AP_MotorsUGV::output_test_pct(motor_test_order motor_seq, float pct)
{
    // check if the motor_seq is valid
    if (motor_seq >= MOTOR_TEST_LAST) {
        return false;
    }
    pct = constrain_float(pct, -100.0f, 100.0f);

    switch (motor_seq) {
        case MOTOR_TEST_THROTTLE: {
            if (SRV_Channels::function_assigned(SRV_Channel::k_motor1)) {
                output_throttle(SRV_Channel::k_motor1, pct);
            }
            if (SRV_Channels::function_assigned(SRV_Channel::k_throttle)) {
                output_throttle(SRV_Channel::k_throttle, pct);
            }
            break;
        }
        case MOTOR_TEST_STEERING: {
            if (SRV_Channels::function_assigned(SRV_Channel::k_motor2)) {
                output_throttle(SRV_Channel::k_motor2, pct);
            }
            if (SRV_Channels::function_assigned(SRV_Channel::k_steering)) {
                SRV_Channels::set_output_scaled(SRV_Channel::k_steering, pct * 45.0f);
            }
            break;
        }
        case MOTOR_TEST_THROTTLE_LEFT: {
            if (SRV_Channels::function_assigned(SRV_Channel::k_motor3)) {
                output_throttle(SRV_Channel::k_motor3, pct);
            }
            if (SRV_Channels::function_assigned(SRV_Channel::k_throttleLeft)) {
                output_throttle(SRV_Channel::k_throttleLeft, pct);
            }
            break;
        }
        case MOTOR_TEST_THROTTLE_RIGHT: {
            if (SRV_Channels::function_assigned(SRV_Channel::k_motor4)) {
                output_throttle(SRV_Channel::k_motor4, pct);
            }
            if (SRV_Channels::function_assigned(SRV_Channel::k_throttleRight)) {
                output_throttle(SRV_Channel::k_throttleRight, pct);
            }
            break;
        }
        case MOTOR_TEST_MAINSAIL: {
            if (SRV_Channels::function_assigned(SRV_Channel::k_mainsail_sheet)) {
                SRV_Channels::set_output_scaled(SRV_Channel::k_mainsail_sheet, pct);
            }
            if (SRV_Channels::function_assigned(SRV_Channel::k_wingsail_elevator)) {
                SRV_Channels::set_output_scaled(SRV_Channel::k_wingsail_elevator, pct);
            }
            if (SRV_Channels::function_assigned(SRV_Channel::k_mast_rotation)) {
                SRV_Channels::set_output_scaled(SRV_Channel::k_mast_rotation, pct);
            }
            break;
        }
        case MOTOR_TEST_LAST:
            return false;
    }
    auto &srv = AP::srv();
    SRV_Channels::calc_pwm();
    srv.cork();
    SRV_Channels::output_ch_all();
    srv.push();
    return true;
}

// test steering or throttle output using a pwm value
bool AP_MotorsUGV::output_test_pwm(motor_test_order motor_seq, float pwm)
{
    // check if the motor_seq is valid
    if (motor_seq > MOTOR_TEST_THROTTLE_RIGHT) {
        return false;
    }
    switch (motor_seq) {
        case MOTOR_TEST_THROTTLE: {
            if (SRV_Channels::function_assigned(SRV_Channel::k_motor1)) {
                SRV_Channels::set_output_pwm(SRV_Channel::k_motor1, pwm);
            }
            if (SRV_Channels::function_assigned(SRV_Channel::k_throttle)) {
                SRV_Channels::set_output_pwm(SRV_Channel::k_throttle, pwm);
            }
            break;
        }
        case MOTOR_TEST_STEERING: {
            if (SRV_Channels::function_assigned(SRV_Channel::k_motor2)) {
                SRV_Channels::set_output_pwm(SRV_Channel::k_motor2, pwm);
            }
            if (SRV_Channels::function_assigned(SRV_Channel::k_steering)) {
                SRV_Channels::set_output_pwm(SRV_Channel::k_steering, pwm);
            }
            break;
        }
        case MOTOR_TEST_THROTTLE_LEFT: {
            if (SRV_Channels::function_assigned(SRV_Channel::k_motor3)) {
                SRV_Channels::set_output_pwm(SRV_Channel::k_motor3, pwm);
            }
            if (SRV_Channels::function_assigned(SRV_Channel::k_throttleLeft)) {
                SRV_Channels::set_output_pwm(SRV_Channel::k_throttleLeft, pwm);
            }
            break;
        }
        case MOTOR_TEST_THROTTLE_RIGHT: {
            if (SRV_Channels::function_assigned(SRV_Channel::k_motor4)) {
                SRV_Channels::set_output_pwm(SRV_Channel::k_motor4, pwm);
            }
            if (SRV_Channels::function_assigned(SRV_Channel::k_throttleRight)) {
                SRV_Channels::set_output_pwm(SRV_Channel::k_throttleRight, pwm);
            }
            break;
        }
        case MOTOR_TEST_MAINSAIL: {
            if (SRV_Channels::function_assigned(SRV_Channel::k_mainsail_sheet)) {
                SRV_Channels::set_output_pwm(SRV_Channel::k_mainsail_sheet, pwm);
            }
            if (SRV_Channels::function_assigned(SRV_Channel::k_wingsail_elevator)) {
                SRV_Channels::set_output_pwm(SRV_Channel::k_wingsail_elevator, pwm);
            }
            if (SRV_Channels::function_assigned(SRV_Channel::k_mast_rotation)) {
                SRV_Channels::set_output_pwm(SRV_Channel::k_mast_rotation, pwm);
            }
            break;
        }
        default:
            return false;
    }
    auto &srv = AP::srv();
    SRV_Channels::calc_pwm();
    srv.cork();
    SRV_Channels::output_ch_all();
    srv.push();
    return true;
}

//  returns true if checks pass, false if they fail.  report should be true to send text messages to GCS
bool AP_MotorsUGV::pre_arm_check(bool report) const
{
    const bool have_throttle = SRV_Channels::function_assigned(SRV_Channel::k_throttle);
    const bool have_throttle_left = SRV_Channels::function_assigned(SRV_Channel::k_throttleLeft);
    const bool have_throttle_right = SRV_Channels::function_assigned(SRV_Channel::k_throttleRight);

    // check that there's defined outputs, inc scripting and sail
    if(!have_throttle_left &&
       !have_throttle_right &&
       !have_throttle &&
       !SRV_Channels::function_assigned(SRV_Channel::k_steering) &&
       !SRV_Channels::function_assigned(SRV_Channel::k_scripting1) &&
       !has_sail() &&
       !is_omni()) {
        if (report) {
            GCS_SEND_TEXT(MAV_SEVERITY_CRITICAL, "PreArm: no motor, sail or scripting outputs defined");
        }
        return false;
    }
    // check if only one of skid-steering output has been configured
    if (have_throttle_left != have_throttle_right) {
        if (report) {
            GCS_SEND_TEXT(MAV_SEVERITY_CRITICAL, "PreArm: check skid steering config");
        }
        return false;
    }
    // check if only one of throttle or steering outputs has been configured, if has a sail allow no throttle
    if ((has_sail() || have_throttle) != SRV_Channels::function_assigned(SRV_Channel::k_steering)) {
        if (report) {
            GCS_SEND_TEXT(MAV_SEVERITY_CRITICAL, "PreArm: check steering and throttle config");
        }
        return false;
    }
    // check all omni motor outputs have been configured
    for (uint8_t i=0; i<_motors_num; i++) {
        SRV_Channel::Function function = SRV_Channels::get_motor_function(i);
        if (!SRV_Channels::function_assigned(function)) {
            if (report) {
                GCS_SEND_TEXT(MAV_SEVERITY_CRITICAL, "PreArm: servo function %u unassigned", function);
            }
            return false;
        }
    }

    // Check relays are configured for brushed with relay outputs
#if AP_RELAY_ENABLED
    AP_Relay*relay = AP::relay();
    if ((_pwm_type == PWMType::BRUSHED_WITH_RELAY) && (relay != nullptr)) {
        // If a output is configured its relay must be enabled
        struct RelayTable {
            bool output_assigned;
            AP_Relay_Params::FUNCTION fun;
        };

        const RelayTable relay_table[] = {
            { have_throttle || have_throttle_left || (SRV_Channels::function_assigned(SRV_Channel::k_motor1) && (_motors_num >= 1)), AP_Relay_Params::FUNCTION::BRUSHED_REVERSE_1 },
            { have_throttle_right || (SRV_Channels::function_assigned(SRV_Channel::k_motor2) && (_motors_num >= 2)),                 AP_Relay_Params::FUNCTION::BRUSHED_REVERSE_2 },
            { SRV_Channels::function_assigned(SRV_Channel::k_motor3) && (_motors_num >= 3),                                          AP_Relay_Params::FUNCTION::BRUSHED_REVERSE_3 },
            { SRV_Channels::function_assigned(SRV_Channel::k_motor4) && (_motors_num >= 4),                                          AP_Relay_Params::FUNCTION::BRUSHED_REVERSE_4 },
        };

        for (uint8_t i=0; i<ARRAY_SIZE(relay_table); i++) {
            if (relay_table[i].output_assigned && !relay->enabled(relay_table[i].fun)) {
                if (report) {
                    GCS_SEND_TEXT(MAV_SEVERITY_CRITICAL, "PreArm: relay function %u unassigned", uint8_t(relay_table[i].fun));
                }
                return false;
            }
        }
    }
#endif

    return true;
}

// sanity check parameters
void AP_MotorsUGV::sanity_check_parameters()
{
    _throttle_min.set(constrain_int16(_throttle_min, 0, 20));
    _throttle_max.set(constrain_int16(_throttle_max, 30, 100));
    _vector_angle_max.set(constrain_float(_vector_angle_max, 0.0f, 90.0f));
}

// setup pwm output type
void AP_MotorsUGV::setup_pwm_type()
{
    _motor_mask = 0;

    hal.rcout->set_dshot_esc_type(SRV_Channels::get_dshot_esc_type());
    
    // work out mask of channels assigned to motors
    _motor_mask |= SRV_Channels::get_output_channel_mask(SRV_Channel::k_throttle);
    _motor_mask |= SRV_Channels::get_output_channel_mask(SRV_Channel::k_throttleLeft);
    _motor_mask |= SRV_Channels::get_output_channel_mask(SRV_Channel::k_throttleRight);
    for (uint8_t i=0; i<_motors_num; i++) {
        _motor_mask |= SRV_Channels::get_output_channel_mask(SRV_Channels::get_motor_function(i));
    }

    switch (_pwm_type) {
    case PWMType::ONESHOT:
        hal.rcout->set_output_mode(_motor_mask, AP_HAL::RCOutput::MODE_PWM_ONESHOT);
        break;
    case PWMType::ONESHOT125:
        hal.rcout->set_output_mode(_motor_mask, AP_HAL::RCOutput::MODE_PWM_ONESHOT125);
        break;
    case PWMType::BRUSHED_WITH_RELAY:
    case PWMType::BRUSHED_BIPOLAR:
        hal.rcout->set_output_mode(_motor_mask, AP_HAL::RCOutput::MODE_PWM_BRUSHED);
        hal.rcout->set_freq(_motor_mask, uint16_t(_pwm_freq * 1000));
        break;
    case PWMType::DSHOT150:
        hal.rcout->set_output_mode(_motor_mask, AP_HAL::RCOutput::MODE_PWM_DSHOT150);
        break;
    case PWMType::DSHOT300:
        hal.rcout->set_output_mode(_motor_mask, AP_HAL::RCOutput::MODE_PWM_DSHOT300);
        break;
    case PWMType::DSHOT600:
        hal.rcout->set_output_mode(_motor_mask, AP_HAL::RCOutput::MODE_PWM_DSHOT600);
        break;
    case PWMType::DSHOT1200:
        hal.rcout->set_output_mode(_motor_mask, AP_HAL::RCOutput::MODE_PWM_DSHOT1200);
        break;
    default:
        // do nothing
        break;
    }
}

// setup stepper steering
void AP_MotorsUGV::setup_stepper_ctrl(){
    // setup stepper control
    if (_stepper_ctrl.is_active) {
        // TODO: Move this to a more appropriate place, create own driver for analog encoder or make a generic one.
        _encoder_analog_source = hal.analogin->channel(0);
        hal.rcout->set_output_mode(SRV_Channels::get_output_channel_mask(SRV_Channel::k_steering), AP_HAL::RCOutput::MODE_PWM_BRUSHED);
    }
}

// setup for frames with omni motors
void AP_MotorsUGV::setup_omni()
{
    // remove existing motors
    for (int8_t i=0; i<AP_MOTORS_NUM_MOTORS_MAX; i++) {
        clear_omni_motors(i);
    }

    // hard coded factor configuration
    switch (_frame_type) {

    //   FRAME TYPE NAME
    case FRAME_TYPE_UNDEFINED:
        break;

    case FRAME_TYPE_OMNI3:
        _motors_num = 3;
        add_omni_motor(0, 1.0f, -1.0f, -1.0f);
        add_omni_motor(1, 0.0f, -1.0f, 1.0f);
        add_omni_motor(2, 1.0f, 1.0f, 1.0f);
        break;

    case FRAME_TYPE_OMNIX:
        _motors_num = 4,
        add_omni_motor(0, 1.0f, -1.0f, -1.0f);
        add_omni_motor(1, 1.0f, -1.0f, 1.0f);
        add_omni_motor(2, 1.0f, 1.0f, -1.0f);
        add_omni_motor(3, 1.0f, 1.0f, 1.0f);
        break;

    case FRAME_TYPE_OMNIPLUS:
        _motors_num = 4;
        add_omni_motor(0, 0.0f, 1.0f, 1.0f);
        add_omni_motor(1, 1.0f, 0.0f, 0.0f);
        add_omni_motor(2, 0.0f, -1.0f, 1.0f);
        add_omni_motor(3, 1.0f, 0.0f, 0.0f);
        break;

    case FRAME_TYPE_OMNI3MECANUM:
        _motors_num = 3;
        add_omni_motor(0,  -1.0f,    1.0f,  -0.26795f);
        add_omni_motor(1,  0.73205f, 1.0f,  -0.73205f);
        add_omni_motor(2,  0.26795f, 1.0f,   1.0f);
        break;
    }
}

// add omni motor using separate throttle, steering and lateral factors
void AP_MotorsUGV::add_omni_motor(int8_t motor_num, float throttle_factor, float steering_factor, float lateral_factor)
{
    // ensure valid motor number is provided
    if (motor_num >= 0 && motor_num < AP_MOTORS_NUM_MOTORS_MAX) {

        // set throttle, steering and lateral factors
        _throttle_factor[motor_num] = throttle_factor;
        _steering_factor[motor_num] = steering_factor;
        _lateral_factor[motor_num] = lateral_factor;

        add_omni_motor_num(motor_num);
    }
}

// add an omni motor and set up default output function
void AP_MotorsUGV::add_omni_motor_num(int8_t motor_num)
{
    // ensure a valid motor number is provided
    if (motor_num >= 0 && motor_num < AP_MOTORS_NUM_MOTORS_MAX) {
        uint8_t chan;
        SRV_Channel::Function function = SRV_Channels::get_motor_function(motor_num);
        SRV_Channels::set_aux_channel_default(function, motor_num);
        if (!SRV_Channels::find_channel(function, chan)) {
            GCS_SEND_TEXT(MAV_SEVERITY_ERROR, "Motors: unable to setup motor %u", motor_num);
        }
    }
}

// disable omni motor and remove all throttle, steering and lateral factor for this motor
void AP_MotorsUGV::clear_omni_motors(int8_t motor_num)
{
    // ensure valid motor number is provided
    if (motor_num >= 0 && motor_num < AP_MOTORS_NUM_MOTORS_MAX) {
        // disable the motor and set factors to zero
        _throttle_factor[motor_num] = 0;
        _steering_factor[motor_num] = 0;
        _lateral_factor[motor_num] = 0;
    }
}

// output to regular steering and throttle channels
void AP_MotorsUGV::output_regular(bool armed, float ground_speed, float steering, float throttle)
{
    // output to throttle channels
    if (armed) {
        if (_scale_steering) {
            // vectored thrust handling
            if (have_vectored_thrust()) {

                // normalise desired steering and throttle to ease calculations
                float steering_norm = steering / 4500.0f;
                const float throttle_norm = throttle * 0.01f;

                // steering can never be more than throttle * tan(_vector_angle_max)
                const float vector_angle_max_rad = radians(constrain_float(_vector_angle_max, 0.0f, 90.0f));
                const float steering_norm_lim = fabsf(throttle_norm * tanf(vector_angle_max_rad));
                if (fabsf(steering_norm) > steering_norm_lim) {
                    if (is_positive(steering_norm)) {
                        steering_norm = steering_norm_lim;
                    }
                    if (is_negative(steering_norm)) {
                        steering_norm = -steering_norm_lim;
                    }
                    limit.steer_right = true;
                    limit.steer_left = true;
                }

                if (!is_zero(throttle_norm)) {
                    // calculate steering angle
                    float steering_angle_rad = atanf(steering_norm / throttle_norm);
                    // limit steering angle to vector_angle_max
                    if (fabsf(steering_angle_rad) > vector_angle_max_rad) {
                        steering_angle_rad = constrain_float(steering_angle_rad, -vector_angle_max_rad, vector_angle_max_rad);
                        limit.steer_right = true;
                        limit.steer_left = true;
                     }

                    // convert steering angle to steering output
                    steering = steering_angle_rad / vector_angle_max_rad * 4500.0f;

                    // scale up throttle to compensate for steering angle
                    const float throttle_scaler_inv = cosf(steering_angle_rad);
                    if (!is_zero(throttle_scaler_inv)) {
                        throttle /= throttle_scaler_inv;
                    }
                }
            } else {
                // scale steering down as speed increase above MOT_SPD_SCA_BASE (1 m/s default)
                if (is_positive(_speed_scale_base) && (fabsf(ground_speed) > _speed_scale_base)) {
                    steering *= (_speed_scale_base / fabsf(ground_speed));
                } else {
                    // regular steering rover at low speed so set limits to stop I-term build-up in controllers
                    if (!have_skid_steering()) {
                        limit.steer_left = true;
                        limit.steer_right = true;
                    }
                }
                // reverse steering direction when backing up
                if (is_negative(ground_speed)) {
                    steering *= -1.0f;
                }
            }
        } else {
            // reverse steering direction when backing up
            if (is_negative(throttle)) {
                steering *= -1.0f;
            }
        }
        output_throttle(SRV_Channel::k_throttle, throttle);
    } else {
        // handle disarmed case
        if (_disarm_disable_pwm) {
            SRV_Channels::set_output_limit(SRV_Channel::k_throttle, SRV_Channel::Limit::ZERO_PWM);
        } else {
            SRV_Channels::set_output_limit(SRV_Channel::k_throttle, SRV_Channel::Limit::TRIM);
        }
    }

    // clear and set limits based on input
    // we do this here because vectored thrust or speed scaling may have reduced steering request
    set_limits_from_input(armed, steering, throttle);

    // constrain steering
    steering = constrain_float(steering, -4500.0f, 4500.0f);

    // always allow steering to move
    // If stepper control is active, this means we are using speed control by adjusting PWM freq.
    if (_stepper_ctrl.is_active) {
         IGNORE_RETURN(_encoder_analog_source->set_pin(_stepper_ctrl.encoder_analog_pin));
        _stepper_ctrl.setpoint = steering/100.0f;
        const float steering_meas = ((_encoder_analog_source->voltage_latest()/2.0f) * (360.0f/3.3f)) - 180.0f;
        _stepper_ctrl.update(steering_meas);
        // GCS_SEND_TEXT(MAV_SEVERITY_DEBUG, "STEERING: %f %f", steering_meas, _encoder_analog_source->voltage_latest());
        // GCS_SEND_TEXT(MAV_SEVERITY_DEBUG, "ABS ANGLE: %f", _encoder_analog_source->voltage_latest()/2.0f * (360.0f/5.0f));
        // _stepper_ctrl.update(((_encoder_analog_source->voltage_latest()/2) * (360.0f/5.0f)) - 180);
    };
    SRV_Channels::set_output_scaled(SRV_Channel::k_steering, steering);
}

// output to skid steering channels
void AP_MotorsUGV::output_skid_steering(bool armed, float steering, float throttle, float dt)
{
    if (!have_skid_steering()) {
        return;
    }

    // clear and set limits based on input
    set_limits_from_input(armed, steering, throttle);

    // constrain steering
    steering = constrain_float(steering, -4500.0f, 4500.0f);

    // handle simpler disarmed case
    if (!armed) {
        if (_disarm_disable_pwm) {
            SRV_Channels::set_output_limit(SRV_Channel::k_throttleLeft, SRV_Channel::Limit::ZERO_PWM);
            SRV_Channels::set_output_limit(SRV_Channel::k_throttleRight, SRV_Channel::Limit::ZERO_PWM);
        } else {
            SRV_Channels::set_output_limit(SRV_Channel::k_throttleLeft, SRV_Channel::Limit::TRIM);
            SRV_Channels::set_output_limit(SRV_Channel::k_throttleRight, SRV_Channel::Limit::TRIM);
        }
        return;
    }

    // skid steering mixer
    float steering_scaled = steering / 4500.0f; // steering scaled -1 to +1
    float throttle_scaled = throttle * 0.01f;  // throttle scaled -1 to +1

    // sanitize values for asymmetry of thrust, mixer assumes forward thrust is always larger than reverse
    const float thrust_asymmetry = MAX(_thrust_asymmetry, 1.0);
    const float lower_throttle_limit = -1.0 / thrust_asymmetry;

    // Maximum steering is half way between upper and lower limits
    const float best_steering_throttle = (1.0 + lower_throttle_limit) * 0.5;
    float steering_range;
    if (throttle_scaled < best_steering_throttle) {
        // steering range is reduced as throttle will never be increased by mixer
        steering_range = MAX(throttle_scaled,0.0) - lower_throttle_limit;
    } else {
        // full range available, throttle can always be lowered down to best_steering_throttle
        steering_range = 1 - best_steering_throttle;
    }

    // apply constraints
    if (steering_scaled > steering_range) {
        limit.steer_right = true;
        steering_scaled = steering_range;
    } else if (steering_scaled < -steering_range) {
        limit.steer_left = true;
        steering_scaled = -steering_range;
    }
    if (throttle_scaled > 1.0) {
        limit.throttle_upper = true;
        throttle_scaled = 1.0;
    } else if (throttle_scaled < lower_throttle_limit) {
        limit.throttle_lower = true;
        throttle_scaled = lower_throttle_limit;
    }

    // All throttle or all steering will now fit, check if they will both fit together
    const float max_output = throttle_scaled + fabsf(steering_scaled);
    const float min_output = throttle_scaled - fabsf(steering_scaled);

    // check for saturation and scale back throttle and steering proportionally
    const float saturation_value = MAX(max_output, min_output / lower_throttle_limit);
    if (saturation_value > 1.0f) {
        // store pre-scaled values so we can set limit flags afterwards
        const float steering_scaled_orig = steering_scaled;
        const float throttle_scaled_orig = throttle_scaled;

        const float str_thr_mix = constrain_float(_steering_throttle_mix, 0.0f, 1.0f);
        const float fair_scaler = 1.0f / saturation_value;
        if (str_thr_mix >= 0.5f) {
            // prioritise steering over throttle
            steering_scaled *= linear_interpolate(fair_scaler, 1.0f, str_thr_mix, 0.5f, 1.0f);
            if (throttle_scaled >= best_steering_throttle) {
                // constrained by upper limit
                throttle_scaled = 1.0 - fabsf(steering_scaled);
            } else {
                // constrained by lower limit
                throttle_scaled = fabsf(steering_scaled) + lower_throttle_limit;
            }

        } else {
            // prioritise throttle over steering
            throttle_scaled *= linear_interpolate(fair_scaler, 1.0f, 0.5f - str_thr_mix, 0.0f, 0.5f);
            const float steering_sign = is_positive(steering_scaled) ? 1.0 : -1.0;
            if (throttle_scaled >= best_steering_throttle) {
                // constrained by upper limit
                steering_scaled = (1.0 - throttle_scaled) * steering_sign;
            } else {
                // constrained by lower limit
                steering_scaled = (throttle_scaled - lower_throttle_limit) * steering_sign;
            }
        }

        // update limits if either steering or throttle has been reduced
        if (fabsf(steering_scaled) < fabsf(steering_scaled_orig)) {
            limit.steer_left |= is_negative(steering_scaled_orig);
            limit.steer_right |= is_positive(steering_scaled_orig);
        }
        if (fabsf(throttle_scaled) < fabsf(throttle_scaled_orig)) {
            limit.throttle_lower |= is_negative(throttle_scaled_orig);
            limit.throttle_upper |= is_positive(throttle_scaled_orig);
        }
    }

    // add in throttle and steering
    float motor_left = throttle_scaled + steering_scaled;
    float motor_right = throttle_scaled - steering_scaled;

    // Apply asymmetry correction
    if (is_negative(motor_right)) {
        motor_right *= thrust_asymmetry;
    }
    if (is_negative(motor_left)) {
        motor_left *= thrust_asymmetry;
    }

    // send pwm value to each motor
    output_throttle(SRV_Channel::k_throttleLeft, 100.0f * motor_left, dt);
    output_throttle(SRV_Channel::k_throttleRight, 100.0f * motor_right, dt);
}

// output for omni frames
void AP_MotorsUGV::output_omni(bool armed, float steering, float throttle, float lateral)
{
    // exit immediately if the vehicle is not omni
    if (!is_omni()) {
        return;
    }

    if (armed) {
        // clear and set limits based on input
        set_limits_from_input(armed, steering, throttle);

        // constrain steering
        steering = constrain_float(steering, -4500.0f, 4500.0f);

        // scale throttle, steering and lateral inputs to -1 to 1
        const float scaled_throttle = throttle * 0.01f;
        const float scaled_steering = steering / 4500.0f;
        const float scaled_lateral = lateral * 0.01f;

        float thr_str_ltr_out[_motors_num];
        float thr_str_ltr_max = 1;
        for (uint8_t i=0; i<_motors_num; i++) {
            // Each motor outputs throttle + steering + lateral
            thr_str_ltr_out[i] = (scaled_throttle * _throttle_factor[i]) +
                              (scaled_steering * _steering_factor[i]) +
                              (scaled_lateral * _lateral_factor[i]);
            // record the largest output above 1
            if (fabsf(thr_str_ltr_out[i]) > thr_str_ltr_max) {
                thr_str_ltr_max = fabsf(thr_str_ltr_out[i]);
            }
        }
        // Scale all outputs back evenly such that the largest fits
        const float output_scale = 1 / thr_str_ltr_max;
        for (uint8_t i=0; i<_motors_num; i++) {
            // send output for each motor
            output_throttle(SRV_Channels::get_motor_function(i), thr_str_ltr_out[i] * 100.0f * output_scale);
        }
        if (output_scale < 1.0) {
            // can't tell which command resulted in the scale back, so limit all
            limit.steer_left = true;
            limit.steer_right = true;
            limit.throttle_lower = true;
            limit.throttle_upper = true;
        }
    } else {
        // handle disarmed case
        if (_disarm_disable_pwm) {
            for (uint8_t i=0; i<_motors_num; i++) {
                SRV_Channels::set_output_limit(SRV_Channels::get_motor_function(i), SRV_Channel::Limit::ZERO_PWM);
            }
        } else {
            for (uint8_t i=0; i<_motors_num; i++) {
                SRV_Channels::set_output_limit(SRV_Channels::get_motor_function(i), SRV_Channel::Limit::TRIM);
            }
        }
    }
}

// output throttle value to main throttle channel, left throttle or right throttle.  throttle should be scaled from -100 to 100
void AP_MotorsUGV::output_throttle(SRV_Channel::Function function, float throttle, float dt)
{
    // sanity check servo function
    if (function != SRV_Channel::k_throttle && function != SRV_Channel::k_throttleLeft && function != SRV_Channel::k_throttleRight && function != SRV_Channel::k_motor1 && function != SRV_Channel::k_motor2 && function != SRV_Channel::k_motor3 && function!= SRV_Channel::k_motor4) {
        return;
    }

    // constrain and scale output
    throttle = get_scaled_throttle(throttle);

    // apply rate control
    throttle = get_rate_controlled_throttle(function, throttle, dt);

    // set relay if necessary
#if AP_RELAY_ENABLED
    AP_Relay*relay = AP::relay();
    if ((_pwm_type == PWMType::BRUSHED_WITH_RELAY) && (relay != nullptr)) {

        // find the output channel, if not found return
        const SRV_Channel *out_chan = SRV_Channels::get_channel_for(function);
        if (out_chan == nullptr) {
            return;
        }
        const int8_t reverse_multiplier = out_chan->get_reversed() ? -1 : 1;
        bool relay_high = is_negative(reverse_multiplier * throttle);

        AP_Relay_Params::FUNCTION relay_function;
        switch (function) {
            case SRV_Channel::k_throttle:
            case SRV_Channel::k_throttleLeft:
            case SRV_Channel::k_motor1:
            default:
                relay_function = AP_Relay_Params::FUNCTION::BRUSHED_REVERSE_1;
                break;
            case SRV_Channel::k_throttleRight:
            case SRV_Channel::k_motor2:
                relay_function = AP_Relay_Params::FUNCTION::BRUSHED_REVERSE_2;
                break;
            case SRV_Channel::k_motor3:
                relay_function = AP_Relay_Params::FUNCTION::BRUSHED_REVERSE_3;
                break;
            case SRV_Channel::k_motor4:
                relay_function = AP_Relay_Params::FUNCTION::BRUSHED_REVERSE_4;
                break;
        }
        relay->set(relay_function, relay_high);

        // invert the output to always have positive value calculated by calc_pwm
        throttle = reverse_multiplier * fabsf(throttle);
    }
#endif  // AP_RELAY_ENABLED

    if (_reverse_delay > 0) {
        switch (function) {
        case SRV_Channel::k_throttle:
            rev_delay_throttle.output(function, throttle, _reverse_delay);
            return;
        case SRV_Channel::k_throttleLeft:
            rev_delay_throttleLeft.output(function, throttle * 10, _reverse_delay);
            return;
        case SRV_Channel::k_throttleRight:
            rev_delay_throttleRight.output(function, throttle * 10, _reverse_delay);
            return;
        default:
            // fall through to other non-delayed outputs
            break;
        }
    }

    // output to servo channel
    switch (function) {
        case SRV_Channel::k_throttle:
        case SRV_Channel::k_motor1:
        case SRV_Channel::k_motor2:
        case SRV_Channel::k_motor3:
        case SRV_Channel::k_motor4:
            SRV_Channels::set_output_scaled(function,  throttle);
            break;
        case SRV_Channel::k_throttleLeft:
        case SRV_Channel::k_throttleRight:
            SRV_Channels::set_output_scaled(function,  throttle * 10.0f);
            break;
        default:
            // do nothing
            break;
    }
}

// output for sailboat's sails
void AP_MotorsUGV::output_sail()
{
    if (!has_sail()) {
        return;
    }

    SRV_Channels::set_output_scaled(SRV_Channel::k_mainsail_sheet, _mainsail);
    SRV_Channels::set_output_scaled(SRV_Channel::k_wingsail_elevator, _wingsail);
    SRV_Channels::set_output_scaled(SRV_Channel::k_mast_rotation, _mast_rotation);
}

// slew limit throttle for one iteration
void AP_MotorsUGV::slew_limit_throttle(float dt)
{
    const float throttle_orig = _throttle;
    _throttle = get_slew_limited_throttle(_throttle, dt);
    if (throttle_orig > _throttle) {
        limit.throttle_upper = true;
    } else if (throttle_orig < _throttle) {
        limit.throttle_lower = true;
    }
    _throttle_prev = _throttle;
}

// set limits based on steering and throttle input
void AP_MotorsUGV::set_limits_from_input(bool armed, float steering, float throttle)
{
    // set limits based on inputs
    limit.steer_left |= !armed || (steering <= -4500.0f);
    limit.steer_right |= !armed || (steering >= 4500.0f);
    limit.throttle_lower |= !armed || (throttle <= -_throttle_max);
    limit.throttle_upper |= !armed || (throttle >= _throttle_max);
}

// scale a throttle using the _throttle_min and _thrust_curve_expo parameters.  throttle should be in the range -100 to +100
float AP_MotorsUGV::get_scaled_throttle(float throttle) const
{
    // exit immediately if throttle is zero
    if (is_zero(throttle)) {
        return throttle;
    }

    // scale using throttle_min
    if (_throttle_min > 0) {
        if (is_negative(throttle)) {
            throttle = -_throttle_min + (throttle * ((100.0f - _throttle_min) * 0.01f));
        } else {
            throttle = _throttle_min + (throttle * ((100.0f - _throttle_min) * 0.01f));
        }
    }

    // skip further scaling if thrust curve disabled or invalid
    if (is_zero(_thrust_curve_expo) || (_thrust_curve_expo > 1.0f) || (_thrust_curve_expo < -1.0f)) {
        return throttle;
    }

    // calculate scaler
    const float sign = (throttle < 0.0f) ? -1.0f : 1.0f;
    const float throttle_pct = constrain_float(throttle, -100.0f, 100.0f) * 0.01f;
    return 100.0f * sign * ((_thrust_curve_expo - 1.0f) + safe_sqrt((1.0f - _thrust_curve_expo) * (1.0f - _thrust_curve_expo) + 4.0f * _thrust_curve_expo * fabsf(throttle_pct))) / (2.0f * _thrust_curve_expo);
}

// use rate controller to achieve desired throttle
float AP_MotorsUGV::get_rate_controlled_throttle(SRV_Channel::Function function, float throttle, float dt)
{
    // require non-zero dt
    if (!is_positive(dt)) {
        return throttle;
    }

    // attempt to rate control left throttle
    if ((function == SRV_Channel::k_throttleLeft) && _rate_controller.enabled(0)) {
        return _rate_controller.get_rate_controlled_throttle(0, throttle, dt);
    }

    // rate control right throttle
    if ((function == SRV_Channel::k_throttleRight) && _rate_controller.enabled(1)) {
        return _rate_controller.get_rate_controlled_throttle(1, throttle, dt);
    }

    // return throttle unchanged
    return throttle;
}

// return true if motors are moving
bool AP_MotorsUGV::active() const
{
    // if soft disarmed, motors not active
    if (!hal.util->get_soft_armed()) {
        return false;
    }

    // check throttle is active
    if (!is_zero(get_throttle())) {
        return true;
    }

    // skid-steering vehicles active when steering
    if (have_skid_steering() && !is_zero(get_steering())) {
        return true;
    }

    return false;
}

// returns true if the configured PWM type is digital and should have fixed endpoints
bool AP_MotorsUGV::is_digital_pwm_type() const
{
    switch (_pwm_type) {
    case PWMType::DSHOT150:
    case PWMType::DSHOT300:
    case PWMType::DSHOT600:
    case PWMType::DSHOT1200:
        return true;
    case PWMType::NORMAL:
    case PWMType::ONESHOT:
    case PWMType::ONESHOT125:
    case PWMType::BRUSHED_WITH_RELAY:
    case PWMType::BRUSHED_BIPOLAR:
        break;
    }
    return false;
}

/*
  handle delay on reversal for a throttle
 */
void AP_MotorsUGV::ReverseThrottle::output(SRV_Channel::Function function, float throttle, float delay)
{
    const uint32_t now_ms = AP_HAL::millis();
    if (is_zero(throttle)) {
        // pass through, no change, don't update the last throttle
    } else if (throttle * last_throttle < 0 &&
        now_ms - last_output_ms < delay*1000) {
        // sign change, add pause
        throttle = 0;
    } else {
        last_output_ms = now_ms;
        last_throttle = throttle;
    }
    SRV_Channels::set_output_scaled(function, throttle);
}

namespace AP {
    AP_MotorsUGV *motors_ugv()
    {
        return AP_MotorsUGV::get_singleton();
    }
}

