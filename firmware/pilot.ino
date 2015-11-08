// radius of Earth in m
#define R 6371000

// minimum speed needed to establish course (knots)
#define MIN_SPEED 1.0

// how close you have to get to the waypoint to consider it hit
#define GET_WITHIN 5

// how much to move tiller by when making slight adjustments
#define FEATHER 5.0

// how to equate two requested rudder positions (i.e. if the new one is less than this from the old one, don't do it)
#define RUDDER_TOLERANCE 1

// adjust course when we're more than this much off-course
#define COURSE_ADJUST_ON 7.0

#define COURSE_ADJUST_SPEED 7.0

// adjust the rudder by this amount to end up at turn of v
#define ADJUST_BY(v) ((v/(FEATHER * COURSE_ADJUST_SPEED)+1.) * FEATHER)

// adjust sails when we're more than this much off-plan
#define SAIL_ADJUST_ON 10

// either side of 0 for "in irons"
#define IRONS 40
#define IN_IRONS(v) (((v) < IRONS || (v) > (360 - IRONS)))
#define TACK_TIMEOUT 10000

// either side of 180 for "running"
#define ON_RUN 20

// either side of 180 for must gybe / might gybe accidentally
#define GYBE_AT 10
#define MIGHT_GYBE(v) (((v) > 180 - GYBE_AT) && ((v) < 180 + GYBE_AT))

// straighten rudder when we make this much of a turn
#define TACK_START_STRAIGHT 50
#define GYBE_START_STRAIGHT 50

#define STALL_SPEED 0.5

// have sails at this position for a gybe
#define GYBE_SAIL_POS 80

// if the course requires zig-zags, turn every x millis
#define TURN_EVERY 90000
#define CAN_TURN() ((last_turn + TURN_EVERY) < millis())
#define COURSE_CORRECTION_TIME 3000

#define SERVO_ORIENTATION -1
#define TO_PORT(amt) rudderTo(current_rudder + (SERVO_ORIENTATION * amt))
#define TO_SBRD(amt) rudderTo(current_rudder - (SERVO_ORIENTATION * amt))
#define RAD(v) ((v) * PI / 180.0)
#define DEG(v) ((v) * 180.0 / PI)

#define WINCH_MIN 60
#define WINCH_MAX 115

// # of waypoints below
#define WP_COUNT 7

// how close we can get to our waypoint before we switch to High Res GPS
#define HRG_THRESHOLD 50

// lat,lon pairs
float wp_list[] = 
  {
    41.923331, -87.631606, 
    41.923374, -87.631205, 
    41.922975, -87.631393, 
    41.923218, -87.631557,
    41.922693, -87.631189,
    41.919746, -87.630085,
    41.916709, -87.628885 // end of circuit
  };
int8_t direction = 1;

// current lat,lon
float wp_lat, wp_lon;
int target_wp = 0;

uint32_t last_turn = 0;

float wp_heading = 0;
float wp_distance = 0;

float ahrs_offset = 0;
uint8_t offset_set = 0;

// not real fusion for now
float fused_heading = 0;

int16_t turning_by = 0;
boolean turning = false;
boolean tacking = false;
boolean stalled = true;

inline void fuseHeading() {
    // no fusion for now. todo: add gps-based mag calibration compensation
    fused_heading = ahrs_heading;
}

// lat/lon and result in radians
float computeBearing(float i_lat, float i_lon, float f_lat, float f_lon) {
    float y = sin(f_lon-i_lon) * cos(f_lat);
    float x = cos(i_lat)*sin(f_lat) - sin(i_lat)*cos(f_lat)*cos(f_lon-i_lon);
    return atan2(y, x); 
}

// lat/lon in radians. returns distance in meters
float computeDistance(float i_lat, float i_lon, float f_lat, float f_lon) {
    float a = sin((f_lat - i_lat)/2) * sin((f_lat - i_lat)/2) +
                        cos(i_lat) * cos(f_lat) *
                        sin((f_lon - i_lon)/2) * sin((f_lon - i_lon)/2);
    float c = 2 * atan2(sqrt(a), sqrt(1-a));
    
    return R * c;
}

// when this is called, we're assuming that we're close to irons, otherwise why tack?
void tack() {
    logln("Tacking...");
    float start_heading = fused_heading;
    uint32_t tack_start_time = millis();

    // yank the tiller, sails are fine
    if (wind > 180) {
        // tacking to port
        // todo: what's the servo direction here?
        TO_PORT(25);
        while (!isPast(start_heading, TACK_START_STRAIGHT, fused_heading, false) && ((millis() - tack_start_time) < TACK_TIMEOUT)) {
            updateSensors();
            fuseHeading();
        }
    } else {
        TO_SBRD(25);
        while (!isPast(start_heading, TACK_START_STRAIGHT, fused_heading, true) && ((millis() - tack_start_time) < TACK_TIMEOUT)) {
            updateSensors();
            fuseHeading();
        }
    }
    
    centerRudder();
    logln("Finished tack");
    // sails are set as is
}

void adjustTo(int amount) {
    int corrected = amount * SERVO_ORIENTATION;
    turning_by = amount;
    turning = true;
    
    rudderFromCenter(corrected);
}

void adjustSails() {
    logln("Checking sail trim");
    float new_winch = map(abs(wind - 180), 30, 170, WINCH_MIN, WINCH_MAX);
    
    if (abs(new_winch - current_winch) > SAIL_ADJUST_ON) {
        logln("New winch position of %d is more than %d off from %d. Adjusting trim.", (int16_t) new_winch, SAIL_ADJUST_ON, current_winch);
        adjustment_made = true;
        winchTo(new_winch);
    } else
        logln("No trim adjustment needed");
}

void adjustHeading() {
    float off_course = angleDiff(fused_heading, wp_heading, true);
    
    if (stalled) {
      off_course = wind > 180 ? 270.0 - wind : 90.0 - wind;
      logln("Stalled. Setting course to beam reach.");
    } else if (IN_IRONS(wind)) {
      off_course = wind > 180 ? 360 - IRONS - wind : IRONS - wind;
      logln("In irons. Setting course for close reach.");
    }
    
    logln("Off course by %d", (int16_t) off_course);
    if (turning) 
      logln("Currently turning by %d, rudder is at %d", turning_by, current_rudder);
    else
      logln("Not currently turning, ruder is at %d", current_rudder);
    
    if (fabs(off_course) > COURSE_ADJUST_ON) {
        logln("Current course is more than %d off target. Trying to adjust", COURSE_ADJUST_ON);

        // new wind if we turn the direction that we want to. The +/-1 is to account for the fact that 
        // rotated rudder will keep us turning
        int new_wind = toCircleDeg(wind - (off_course < 0 ? -1:1));
        
        // if adjusting any more puts us in irons
        if (IN_IRONS(new_wind)) {
            logln("Requested course unsafe, requires tack");
            // and if we're allowed to tack, we tack
            if (CAN_TURN()) {
                logln("Tack change allowed. Turning.");
                tack();
                last_turn = millis();
                
                // tack() centers the rudder, and we should re-evaluate where we are at that point.
                turning_by = 0;
                turning = false;
                adjustment_made = true;
            // if we aren't allowed to tack, but are currently turning, we stop turning
            } else if (turning) {
                logln("Tack change not allowed. Currently in a turn, centering rudder.");
                turning = false;
                turning_by = 0;
                adjustment_made = true;
                centerRudder();
            }
        } else {
            // todo: there's something in ADJUST_BY that's misbehaving with negative off-course values. being lazy here.
            int new_rudder = ADJUST_BY(abs(off_course)) * (off_course >= 0 ? -1 : 1);
            if (abs(new_rudder - turning_by) > RUDDER_TOLERANCE) {
                logln("Adjusting rudder by %d", new_rudder);
                adjustTo(new_rudder);
                adjustment_made = true;
            } else
              logln("New rudder position of %d is close to current rudder position of %d. Not adjusting", new_rudder, turning_by);
        }
    } else if (turning) {
        logln("Current course is not more than %d off target. Centering.", COURSE_ADJUST_ON);
        turning = false;
        turning_by = 0;
        adjustment_made = true;
        centerRudder();
    }
}

void pilotInit() {
    centerRudder();
    centerWinch();
    
    wp_lat = wp_list[0];
    wp_lon = wp_list[1];
}

void processManualCommands() {
    while (Serial.available()) {
        switch ((char)Serial.read()) {
            case 'i':
                updateSensors();
                break;
            case 'a':
                TO_PORT(10);
                logln("10 degrees to port");
                break;
            case 'd':
                TO_SBRD(10);
                logln("10 degrees to starboard");
                break;
            case 's':
                centerRudder();
                logln("Center rudder");
                break;
            case 'q':
                winchTo(current_winch + 5);
                logln("Sheet out");
                break;
            case 'e':
                winchTo(current_winch - 5);
                logln("Sheet in");
                break;
            case 'w':
                centerWinch();
                logln("Center winch");
                break;
            case 'x':
                manual_override = false;
                logln("End manual override");
                break;
        }
    }
}

void setNextWaypoint() {
    logln("Waypoint %d reached.", target_wp);
    
    if ((target_wp == 0 && direction == -1) || (target_wp + 1 == WP_COUNT && direction == 1))
        direction *= -1;
        
    target_wp += direction;
    wp_lat = wp_list[target_wp * 2];
    wp_lon = wp_list[target_wp * 2 + 1];
    
    // wp changed, need to recompute
    wp_distance = computeDistance(RAD(gps_lat), RAD(gps_lon), RAD(wp_lat), RAD(wp_lon));
    wp_heading = DEG(toCircle(computeBearing(RAD(gps_lat), RAD(gps_lon), RAD(wp_lat), RAD(wp_lon))));
    
    // we should allow tacks now, as this mechanism is just to space out zig zags
    // also can just zero it out as there's no inherent value in the time
    last_turn = 0;

    logln("Waypoint %d selected, HTW: %d, DTW: %dm", 
            target_wp,
            ((int16_t) wp_heading), 
            ((int16_t) wp_distance));
}

void doPilot() {
    if (manual_override) {
        processManualCommands();        
        
        return;
    }
    
    stalled = gps_speed < STALL_SPEED;

    // still want to check this
    if ((gps_speed > MIN_SPEED) && gps_updated) {
        ahrs_offset = angleDiff(ahrs_heading, gps_course, true);
        logln("GPS vs AHRS difference is %d", (int16_t) ahrs_offset * 10);
    }

    fuseHeading();        
    
    // check if we've hit the waypoint
    wp_distance = computeDistance(RAD(gps_lat), RAD(gps_lon), RAD(wp_lat), RAD(wp_lon));
    wp_heading = DEG(toCircle(computeBearing(RAD(gps_lat), RAD(gps_lon), RAD(wp_lat), RAD(wp_lon))));
    adjustment_made = false;

    logln("GPS heading: %d, GPS speed (x10): %dkts, HTW: %d, DTW: %dm", 
            ((int16_t) gps_course), 
            ((int16_t) (gps_speed * 10.0)),
            ((int16_t) wp_heading), 
            ((int16_t) wp_distance));

    if (wp_distance < GET_WITHIN)
        setNextWaypoint();
    
    if (wp_distance < HRG_THRESHOLD) {
      high_res_gps = true;
      logln("Within high-res gps threshold. Switching to HRG");
    }
    else
      high_res_gps = HIGH_RES_GPS_DEFAULT;

    adjustHeading();
    adjustSails();
}
