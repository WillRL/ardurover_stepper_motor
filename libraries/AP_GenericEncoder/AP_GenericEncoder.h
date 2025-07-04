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
#pragma once

#include "AP_GenericEncoder_config.h"

#if AP_GENERICENCODER_ENABLED


class AP_GenericEncoder
{
public:
    // constructor. This incorporates initialization as well.
    AP_GenericEncoder();
    virtual ~AP_GenericEncoder(void) {}

    virtual void init(int address);

    // update state
    virtual void update();
    virtual void update_pos();
    virtual void update_velocity();
    virtual void update_acceleration();
    virtual void calibrate();

    void read(float *retvals);

    
  protected:
    // latest values read in
    virtual void setup();
    
    // state variables
    float _pos = 0;                       // position (m or rad)
    float _dt_pos = 0;                     // velocity (m/s or rad/s)
    float _ddt_pos = 0;                    // acceleration (m/s^2 or rad/s^2)
    float _latest_measurement_time = 0;   // latest measurement time (s)

    float _prev_measurement_time = 0;     // previous time (s)
    float _prev_pos = 0;                  // previous position (m or rad)
    float _prev_dt_pos = 0;                // previous velocity (m/s or rad/s)

    static float calc_abs_rotary_d_pos(float old_angle, float new_angle);
};

#endif  // AP_GENERICENCODER_ENABLED