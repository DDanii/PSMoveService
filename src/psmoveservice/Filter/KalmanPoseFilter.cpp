//-- includes --
#include "KalmanPoseFilter.h"
#include "MathAlignment.h"

#include <kalman/MeasurementModel.hpp>
#include <kalman/SystemModel.hpp>
#include <kalman/SquareRootBase.hpp>

//-- constants --
enum StateEnum
{
    POSITION_X, // meters
    LINEAR_VELOCITY_X, // meters / s
    LINEAR_ACCELERATION_X, // meters /s^2
    POSITION_Y,
    LINEAR_VELOCITY_Y,
    LINEAR_ACCELERATION_Y,
    POSITION_Z,
    LINEAR_VELOCITY_Z,
    LINEAR_ACCELERATION_Z,
    ANGLE_AXIS_X,  // axis * radians
    ANGULAR_VELOCITY_X, // rad/s
    ANGLE_AXIS_Y,
    ANGULAR_VELOCITY_Y,
    ANGLE_AXIS_Z,
    ANGULAR_VELOCITY_Z,

    STATE_PARAMETER_COUNT
};

enum PSMoveMeasurementEnum {
    PSMOVE_ACCELEROMETER_X, // gravity units
    PSMOVE_ACCELEROMETER_Y,
    PSMOVE_ACCELEROMETER_Z,
    PSMOVE_GYROSCOPE_X, // rad / s
    PSMOVE_GYROSCOPE_Y,
    PSMOVE_GYROSCOPE_Z,
    PSMOVE_MAGNETOMETER_X,
    PSMOVE_MAGNETOMETER_Y,
    PSMOVE_MAGNETOMETER_Z,
    PSMOVE_OPTICAL_POSITION_X, // meters
    PSMOVE_OPTICAL_POSITION_Y,
    PSMOVE_OPTICAL_POSITION_Z,

    PSMOVE_MEASUREMENT_PARAMETER_COUNT
};

enum DS4MeasurementEnum {
    DS4_ACCELEROMETER_X,
    DS4_ACCELEROMETER_Y,
    DS4_ACCELEROMETER_Z,
    DS4_GYROSCOPE_X,
    DS4_GYROSCOPE_Y,
    DS4_GYROSCOPE_Z,
    DS4_OPTICAL_POSITION_X,
    DS4_OPTICAL_POSITION_Y,
    DS4_OPTICAL_POSITION_Z,
    DS4_OPTICAL_ANGLE_AXIS_X,
    DS4_OPTICAL_ANGLE_AXIS_Y,
    DS4_OPTICAL_ANGLE_AXIS_Z,

    DS4_MEASUREMENT_PARAMETER_COUNT
};

// From: http://nbviewer.jupyter.org/github/rlabbe/Kalman-and-Bayesian-Filters-in-Python/blob/master/10-Unscented-Kalman-Filter.ipynb#Reasonable-Choices-for-the-Parameters
// beta=2 is a good choice for Gaussian problems, 
// kappa=3-n where n is the size of x is a good choice for kappa, 
// 0<=alpha<=1 is an appropriate choice for alpha, 
// where a larger value for alpha spreads the sigma points further from the mean.
#define k_ukf_alpha 1.f
#define k_ukf_beta 2.f
#define k_ukf_kappa -1.f

//-- private methods ---
template <class StateType>
void Q_discrete_3rd_order_white_noise(const float dT, const float var, const int state_index, Kalman::Covariance<StateType> &Q);

template <class StateType>
void Q_discrete_2nd_order_white_noise(const float dT, const float var, const int state_index, Kalman::Covariance<StateType> &Q);

//-- private definitions --
template<typename T>
class PoseStateVector : public Kalman::Vector<T, STATE_PARAMETER_COUNT>
{
public:
    KALMAN_VECTOR(PoseStateVector, T, STATE_PARAMETER_COUNT)

    // Accessors
    Eigen::Vector3f get_position() const { 
        return Eigen::Vector3f((*this)[POSITION_X], (*this)[POSITION_Y], (*this)[POSITION_Z]); 
    }
    Eigen::Vector3f get_linear_velocity() const {
        return Eigen::Vector3f((*this)[LINEAR_VELOCITY_X], (*this)[LINEAR_VELOCITY_Y], (*this)[LINEAR_VELOCITY_Z]);
    }
    Eigen::Vector3f get_linear_acceleration() const {
        return Eigen::Vector3f((*this)[LINEAR_ACCELERATION_X], (*this)[LINEAR_ACCELERATION_Y], (*this)[LINEAR_ACCELERATION_Z]);
    }
    Eigen::AngleAxisf get_angle_axis() const {
        Eigen::Vector3f axis= Eigen::Vector3f((*this)[ANGLE_AXIS_X], (*this)[ANGLE_AXIS_Y], (*this)[ANGLE_AXIS_Z]);
        const float angle= eigen_vector3f_normalize_with_default(axis, Eigen::Vector3f::Zero());
        return Eigen::AngleAxisf(angle, axis);
    }
    Eigen::Quaternionf get_quaternion() const {
        return Eigen::Quaternionf(get_angle_axis());
    }
    Eigen::Vector3f get_angular_velocity() const {
        return Eigen::Vector3f((*this)[ANGULAR_VELOCITY_X], (*this)[ANGULAR_VELOCITY_Y], (*this)[ANGULAR_VELOCITY_Z]);
    }

    // Mutators
    void set_position(const Eigen::Vector3f &p) {
        (*this)[POSITION_X] = p.x(); (*this)[POSITION_Y] = p.y(); (*this)[POSITION_Z] = p.z();
    }
    void set_linear_velocity(const Eigen::Vector3f &v) {
        (*this)[LINEAR_VELOCITY_X] = v.x(); (*this)[LINEAR_VELOCITY_Y] = v.y(); (*this)[LINEAR_VELOCITY_Z] = v.z();
    }
    void set_linear_acceleration(const Eigen::Vector3f &a) {
        (*this)[LINEAR_ACCELERATION_X] = a.x(); (*this)[LINEAR_ACCELERATION_Y] = a.y(); (*this)[LINEAR_ACCELERATION_Z] = a.z();
    }
    void set_angle_axis(const Eigen::AngleAxisf &a) {        
        const float angle= a.angle();
        (*this)[ANGLE_AXIS_X] = a.axis().x() * angle; 
        (*this)[ANGLE_AXIS_Y] = a.axis().y() * angle;
        (*this)[ANGLE_AXIS_Z] = a.axis().z() * angle;
    }
    void set_quaternion(const Eigen::Quaternionf &q) {
        const Eigen::AngleAxisf angle_axis(q);
        set_angle_axis(angle_axis);
    }
    void set_angular_velocity(const Eigen::Vector3f &v) {
        (*this)[ANGULAR_VELOCITY_X] = v.x(); (*this)[ANGULAR_VELOCITY_Y] = v.y(); (*this)[ANGULAR_VELOCITY_Z] = v.z();
    }

	PoseStateVector<T> addition(const PoseStateVector<T> &stateDelta) const
	{
		// Do the default additive delta first
		PoseStateVector<T> result = (*this) + stateDelta;

		// Extract the orientation quaternion from this state (which is stored as an angle axis vector)
		const Eigen::Quaternionf orientation = this->get_quaternion();

		// Extract the delta quaternion (which is also stored as an angle axis vector)
		const Eigen::Quaternionf delta = stateDelta.get_quaternion();

		// Apply the delta to the orientation
		const Eigen::Quaternionf new_rotation = delta*orientation;

		// Stomp over the simple addition of the angle axis result
		result.set_quaternion(new_rotation);

		return result;
	}

	PoseStateVector<T> difference(const PoseStateVector<T> &other) const
	{
		// Do the default state vector substraction first
		PoseStateVector<T> state_diff= (*this) - other;
		
		// Extract the orientation quaternion from both states (which is stored as an angle axis vector)
		const Eigen::Quaternionf q1= this->get_quaternion();
		const Eigen::Quaternionf q2= other.get_quaternion();

		// Compute the "quaternion difference" i.e. rotation from q1 to q2
		const Eigen::Quaternionf q_diff= q2*q1.conjugate();

		// Stomp over the simple subtraction of the angle axis result
		state_diff.set_quaternion(q_diff);

		return state_diff;
	}

	template<int StateCount>
	static PoseStateVector<T> computeWeightedStateAverage(
		const Kalman::Matrix<T, PoseStateVector<T>::RowsAtCompileTime, StateCount>& state_matrix,
		Kalman::Vector<T, StateCount> weight_vector)
	{
		// Use efficient matrix x vector computation to compute a weighted average of the states
		// (the orientation portion will be wrong)
		PoseStateVector<T> result= state_matrix * weight_vector;

		// Extract the orientations from the states
		Eigen::Quaternionf orientations[StateCount];
		float weights[StateCount];
		for (int col_index = 0; col_index <= StateCount; ++col_index)
		{
			const PoseStateVector<T> measurement = state_matrix.col(col_index);
			Eigen::Quaternionf orientation = measurement.get_quaternion();

			orientations[col_index]= orientation;
			weights[col_index]= weight_vector[col_index];
		}

		// Compute the average of the quaternions
		Eigen::Quaternionf average_quat;
		eigen_quaternion_compute_weighted_average(orientations, weights, StateCount, &average_quat);

		// Stomp the incorrect orientation average
		result.set_quaternion(average_quat);

		return result;
	}
};
typedef PoseStateVector<float> PoseStateVectorf;

template<typename T>
class PSMove_MeasurementVector : public Kalman::Vector<T, PSMOVE_MEASUREMENT_PARAMETER_COUNT>
{
public:
    KALMAN_VECTOR(PSMove_MeasurementVector, T, PSMOVE_MEASUREMENT_PARAMETER_COUNT)

    // Accessors
    Eigen::Vector3f get_accelerometer() const {
        return Eigen::Vector3f((*this)[PSMOVE_ACCELEROMETER_X], (*this)[PSMOVE_ACCELEROMETER_Y], (*this)[PSMOVE_ACCELEROMETER_Z]);
    }
    Eigen::Vector3f get_gyroscope() const {
        return Eigen::Vector3f((*this)[PSMOVE_GYROSCOPE_X], (*this)[PSMOVE_GYROSCOPE_Y], (*this)[PSMOVE_GYROSCOPE_Z]);
    }
    Eigen::Vector3f get_magnetometer() const {
        return Eigen::Vector3f((*this)[PSMOVE_MAGNETOMETER_X], (*this)[PSMOVE_MAGNETOMETER_Y], (*this)[PSMOVE_MAGNETOMETER_Z]);
    }
    Eigen::Vector3f get_optical_position() const {
        return Eigen::Vector3f((*this)[PSMOVE_OPTICAL_POSITION_X], (*this)[PSMOVE_OPTICAL_POSITION_Y], (*this)[PSMOVE_OPTICAL_POSITION_Z]);
    }

    // Mutators
    void set_accelerometer(const Eigen::Vector3f &a) {
        (*this)[PSMOVE_ACCELEROMETER_X] = a.x(); (*this)[PSMOVE_ACCELEROMETER_Y] = a.y(); (*this)[PSMOVE_ACCELEROMETER_Z] = a.z();
    }
    void set_gyroscope(const Eigen::Vector3f &g) {
        (*this)[PSMOVE_GYROSCOPE_X] = g.x(); (*this)[PSMOVE_GYROSCOPE_Y] = g.y(); (*this)[PSMOVE_GYROSCOPE_Z] = g.z();
    }
    void set_optical_position(const Eigen::Vector3f &p) {
        (*this)[PSMOVE_OPTICAL_POSITION_X] = p.x(); (*this)[PSMOVE_OPTICAL_POSITION_Y] = p.y(); (*this)[PSMOVE_OPTICAL_POSITION_Z] = p.z();
    }
    void set_magnetometer(const Eigen::Vector3f &m) {
        (*this)[PSMOVE_MAGNETOMETER_X] = m.x(); (*this)[PSMOVE_MAGNETOMETER_Y] = m.y(); (*this)[PSMOVE_MAGNETOMETER_Z] = m.z();
    }

	PSMove_MeasurementVector<T> difference(const PSMove_MeasurementVector<T> &other) const
	{
		// for the PSMove measurement the difference can be computed 
		// with simple vector subtraction
		return (*this) - other;
	}

	template <int StateCount>
	static PSMove_MeasurementVector<T> computeWeightedMeasurementAverage(
		const Kalman::Matrix<T, PSMove_MeasurementVector<T>::RowsAtCompileTime, StateCount>& measurement_matrix,
		const Kalman::Vector<T, StateCount> &weight_vector)
	{
		// Use efficient matrix x vector computation to compute a weighted average of the sigma point samples
		// (No orientation stored in measurement means this can be simple)
		PSMove_MeasurementVector<T> result= measurement_matrix * weight_vector;

		return result;
	}
};
typedef PSMove_MeasurementVector<float> PSMove_MeasurementVectorf;

template<typename T>
class DS4_MeasurementVector : public Kalman::Vector<T, DS4_MEASUREMENT_PARAMETER_COUNT>
{
public:
    KALMAN_VECTOR(DS4_MeasurementVector, T, DS4_MEASUREMENT_PARAMETER_COUNT)

    // Accessors
    Eigen::Vector3f get_accelerometer() const {
        return Eigen::Vector3f((*this)[DS4_ACCELEROMETER_X], (*this)[DS4_ACCELEROMETER_Y], (*this)[DS4_ACCELEROMETER_Z]);
    }
    Eigen::Vector3f get_gyroscope() const {
        return Eigen::Vector3f((*this)[DS4_GYROSCOPE_X], (*this)[DS4_GYROSCOPE_Y], (*this)[DS4_GYROSCOPE_Z]);
    }
    Eigen::Vector3f get_optical_position() const {
        return Eigen::Vector3f((*this)[DS4_OPTICAL_POSITION_X], (*this)[DS4_OPTICAL_POSITION_Y], (*this)[DS4_OPTICAL_POSITION_Z]);
    }
    Eigen::AngleAxisf get_optical_angle_axis() const {
        Eigen::Vector3f axis= Eigen::Vector3f((*this)[DS4_OPTICAL_ANGLE_AXIS_X], (*this)[DS4_OPTICAL_ANGLE_AXIS_Y], (*this)[DS4_OPTICAL_ANGLE_AXIS_Z]);
        const float angle= eigen_vector3f_normalize_with_default(axis, Eigen::Vector3f::Zero());
        return Eigen::AngleAxisf(angle, axis);
    }
    Eigen::Quaternionf get_optical_quaternion() const {
        return Eigen::Quaternionf(get_optical_angle_axis());
    }

    // Mutators
    void set_accelerometer(const Eigen::Vector3f &a) {
        (*this)[DS4_ACCELEROMETER_X] = a.x(); (*this)[DS4_ACCELEROMETER_Y] = a.y(); (*this)[DS4_ACCELEROMETER_Z] = a.z();
    }
    void set_gyroscope(const Eigen::Vector3f &g) {
        (*this)[DS4_GYROSCOPE_X] = g.x(); (*this)[DS4_GYROSCOPE_Y] = g.y(); (*this)[DS4_GYROSCOPE_Z] = g.z();
    }
    void set_optical_position(const Eigen::Vector3f &p) {
        (*this)[DS4_OPTICAL_POSITION_X] = p.x(); (*this)[DS4_OPTICAL_POSITION_Y] = p.y(); (*this)[DS4_OPTICAL_POSITION_Z] = p.z();
    }
    void set_angle_axis(const Eigen::AngleAxisf &a) {        
        const float angle= a.angle();
        (*this)[DS4_OPTICAL_ANGLE_AXIS_X] = a.axis().x() * angle; 
        (*this)[DS4_OPTICAL_ANGLE_AXIS_Y] = a.axis().y() * angle;
        (*this)[DS4_OPTICAL_ANGLE_AXIS_Z] = a.axis().z() * angle;
    }
    void set_optical_quaternion(const Eigen::Quaternionf &q) {
        const Eigen::AngleAxisf angle_axis(q);
        set_angle_axis(angle_axis);
    }

	DS4_MeasurementVector<T> difference(const DS4_MeasurementVector<T> &other) const
	{
		DS4_MeasurementVector<T> measurement_diff= (*this) - other;
		
		const Eigen::Quaternionf q1= this->get_optical_quaternion();
		const Eigen::Quaternionf q2= other.get_optical_quaternion();
		const Eigen::Quaternionf q_diff= q2*q1.conjugate();

		// Stomp the incorrect orientation difference computed by the vector subtraction
		measurement_diff.set_optical_quaternion(q_diff);

		return measurement_diff;
	}

	template<int StateCount>
	static DS4_MeasurementVector<T> computeWeightedMeasurementAverage(
		const Kalman::Matrix<T, DS4_MeasurementVector<T>::RowsAtCompileTime, StateCount>& measurement_matrix,
		Kalman::Vector<T, StateCount> weight_vector)
	{
		// Use efficient matrix x vector computation to compute a weighted average of the measurements
		// (the orientation portion will be wrong)
		DS4_MeasurementVector<T> result= measurement_matrix * weight_vector;

		// Extract the orientations from the measurements
		Eigen::Quaternionf orientations[StateCount];
		float weights[StateCount];
		for (int col_index = 0; col_index <= StateCount; ++col_index)
		{
			const DS4_MeasurementVector<T> measurement = measurement_matrix.col(col_index);
			Eigen::Quaternionf orientation = measurement.get_optical_quaternion();

			orientations[col_index]= orientation;
			weights[col_index]= weight_vector[col_index];
		}

		// Compute the average of the quaternions
		Eigen::Quaternionf average_quat;
		eigen_quaternion_compute_weighted_average(orientations, weights, StateCount, &average_quat);

		// Stomp the incorrect orientation average
		result.set_optical_quaternion(average_quat);

		return result;
	}
};
typedef DS4_MeasurementVector<float> DS4_MeasurementVectorf;

/**
* @brief System model for a controller
*
* This is the system model defining how a controller advances from one
* time-step to the next, i.e. how the system state evolves over time.
*/
class PoseSystemModel : public Kalman::SystemModel<PoseStateVectorf, Kalman::Vector<float, 0>, Kalman::SquareRootBase>
{
public:
    inline void set_time_step(const float dt) { m_time_step = dt; }

    void init(const PoseFilterConstants &constants)
    {
        const float mean_position_dT= constants.position_constants.mean_update_time_delta;
        const float mean_orientation_dT= constants.position_constants.mean_update_time_delta;

        // Start off using the maximum variance values
        const float position_variance= constants.position_constants.max_position_variance;
		const float angle_axis_variance= constants.orientation_constants.max_orientation_variance;

        // Initialize the process covariance matrix Q
        Kalman::Covariance<PoseStateVectorf> Q = Kalman::Covariance<PoseStateVectorf>::Zero();
        Q_discrete_3rd_order_white_noise<PoseStateVectorf>(mean_position_dT, position_variance, POSITION_X, Q);
		Q_discrete_3rd_order_white_noise<PoseStateVectorf>(mean_position_dT, position_variance, POSITION_Y, Q);
		Q_discrete_3rd_order_white_noise<PoseStateVectorf>(mean_position_dT, position_variance, POSITION_Z, Q);
		Q_discrete_2nd_order_white_noise<PoseStateVectorf>(mean_orientation_dT, angle_axis_variance, ANGLE_AXIS_X, Q);
		Q_discrete_2nd_order_white_noise<PoseStateVectorf>(mean_orientation_dT, angle_axis_variance, ANGLE_AXIS_Y, Q);
		Q_discrete_2nd_order_white_noise<PoseStateVectorf>(mean_orientation_dT, angle_axis_variance, ANGLE_AXIS_Z, Q);
        setCovariance(Q);
    }

	void update_process_covariance(
		const PoseFilterConstants &constants,
		const float position_quality,
		const float orientation_quality)
	{
        const float mean_position_dT= constants.position_constants.mean_update_time_delta;
        const float mean_orientation_dT= constants.position_constants.mean_update_time_delta;

        // Start off using the maximum variance values
        const float position_variance= 
			lerp_clampf(
				constants.position_constants.max_position_variance,
				constants.position_constants.min_position_variance,
				position_quality);
		const float angle_axis_variance=
			lerp_clampf(
				constants.orientation_constants.max_orientation_variance,
				constants.orientation_constants.min_orientation_variance,
				orientation_quality);

        // Initialize the process covariance matrix Q
        Kalman::Covariance<PoseStateVectorf> Q = getCovariance();
        Q_discrete_3rd_order_white_noise<PoseStateVectorf>(mean_position_dT, position_variance, POSITION_X, Q);
		Q_discrete_3rd_order_white_noise<PoseStateVectorf>(mean_position_dT, position_variance, POSITION_Y, Q);
		Q_discrete_3rd_order_white_noise<PoseStateVectorf>(mean_position_dT, position_variance, POSITION_Z, Q);
		Q_discrete_2nd_order_white_noise<PoseStateVectorf>(mean_orientation_dT, angle_axis_variance, ANGLE_AXIS_X, Q);
		Q_discrete_2nd_order_white_noise<PoseStateVectorf>(mean_orientation_dT, angle_axis_variance, ANGLE_AXIS_Y, Q);
		Q_discrete_2nd_order_white_noise<PoseStateVectorf>(mean_orientation_dT, angle_axis_variance, ANGLE_AXIS_Z, Q);
        setCovariance(Q);
	}

    /**
    * @brief Definition of (non-linear) state transition function
    *
    * This function defines how the system state is propagated through time,
    * i.e. it defines in which state \f$\hat{x}_{k+1}\f$ is system is expected to
    * be in time-step \f$k+1\f$ given the current state \f$x_k\f$ in step \f$k\f$ and
    * the system control input \f$u\f$.
    *
    * @param [in] x The system state in current time-step
    * @param [in] u The control vector input
    * @returns The (predicted) system state in the next time-step
    */
    PoseStateVectorf f(const PoseStateVectorf& old_state, const Kalman::Vector<float, 0>& control) const
    {
        //! Predicted state vector after transition
        PoseStateVectorf new_state;

        // Extract parameters from the old state
        const Eigen::Vector3f old_position = old_state.get_position();
        const Eigen::Vector3f old_linear_velocity = old_state.get_linear_velocity();
        const Eigen::Vector3f old_linear_acceleration = old_state.get_linear_acceleration();
        const Eigen::Quaternionf old_orientation = old_state.get_quaternion();
        const Eigen::Vector3f old_angular_velocity = old_state.get_angular_velocity();

        // Compute the position state update
        const Eigen::Vector3f new_position= 
            old_position 
            + old_linear_velocity*m_time_step 
            + old_linear_acceleration*m_time_step*m_time_step*0.5f;
        const Eigen::Vector3f new_linear_velocity= old_linear_velocity + old_linear_acceleration*m_time_step;
        const Eigen::Vector3f &new_linear_acceleration= old_linear_acceleration;

        // Compute the orientation update
        const Eigen::Quaternionf quaternion_derivative =
            eigen_angular_velocity_to_quaternion_derivative(old_orientation, old_angular_velocity);
        const Eigen::Quaternionf new_orientation = Eigen::Quaternionf(
            old_orientation.coeffs()
            + quaternion_derivative.coeffs()*m_time_step).normalized();
        const Eigen::Vector3f &new_angular_velocity= old_angular_velocity;

        // Save results to the new state
        new_state.set_position(new_position);
        new_state.set_linear_velocity(new_linear_velocity);
        new_state.set_linear_acceleration(new_linear_acceleration);
        new_state.set_quaternion(new_orientation);
        new_state.set_angular_velocity(new_angular_velocity);

        return new_state;
    }

protected:
    float m_time_step;
};

/**
* @brief Measurement model for measuring PSMove controller
*
* This is the measurement model for measuring the position and magnetometer of the PSMove controller.
* The measurement is given by the optical trackers.
*/
class PSMove_MeasurementModel : public Kalman::MeasurementModel<PoseStateVectorf, PSMove_MeasurementVectorf, Kalman::SquareRootBase>
{
public:
    void init(const PoseFilterConstants &constants)
    {
        Kalman::Covariance<PSMove_MeasurementVectorf> R = 
			Kalman::Covariance<PSMove_MeasurementVectorf>::Zero();

        const float position_variance= constants.position_constants.max_position_variance;
		const float magnetometer_variance= constants.orientation_constants.magnetometer_variance;

        R(PSMOVE_OPTICAL_POSITION_X, PSMOVE_OPTICAL_POSITION_X) = position_variance;
        R(PSMOVE_OPTICAL_POSITION_Y, PSMOVE_OPTICAL_POSITION_Y) = position_variance;
        R(PSMOVE_OPTICAL_POSITION_Z, PSMOVE_OPTICAL_POSITION_Z) = position_variance;
        R(PSMOVE_MAGNETOMETER_X, PSMOVE_MAGNETOMETER_X) = magnetometer_variance;
        R(PSMOVE_MAGNETOMETER_Y, PSMOVE_MAGNETOMETER_Y) = magnetometer_variance;
        R(PSMOVE_MAGNETOMETER_Z, PSMOVE_MAGNETOMETER_Z) = magnetometer_variance;

        setCovariance(R);
        
		m_identity_gravity_direction= constants.orientation_constants.gravity_calibration_direction;
		m_identity_magnetometer_direction= constants.orientation_constants.magnetometer_calibration_direction;
    }

	void update_measurement_covariance(
		const PoseFilterConstants &constants,
		const float position_quality)
	{
        // Start off using the maximum variance values
        const float position_variance= 
			lerp_clampf(
				constants.position_constants.max_position_variance,
				constants.position_constants.min_position_variance,
				position_quality);

        // Update the measurement covariance R
        Kalman::Covariance<PSMove_MeasurementVectorf> R = getCovariance();
        R(PSMOVE_OPTICAL_POSITION_X, PSMOVE_OPTICAL_POSITION_X) = position_variance;
        R(PSMOVE_OPTICAL_POSITION_Y, PSMOVE_OPTICAL_POSITION_Y) = position_variance;
        R(PSMOVE_OPTICAL_POSITION_Z, PSMOVE_OPTICAL_POSITION_Z) = position_variance;
        setCovariance(R);
	}

    /**
    * @brief Definition of (possibly non-linear) measurement function
    *
    * This function maps the system state to the measurement that is expected
    * to be received from the sensor assuming the system is currently in the
    * estimated state.
    *
    * @param [in] x The system state in current time-step
    * @returns The (predicted) sensor measurement for the system state
    */
    PSMove_MeasurementVectorf h(const PoseStateVectorf& x) const
    {
        PSMove_MeasurementVectorf predicted_measurement;

        // Use the position and orientation from the state for predictions
        const Eigen::Vector3f position= x.get_position();
        const Eigen::Quaternionf orientation= x.get_quaternion();

        // Use the current linear acceleration from the state to predict
        // what the accelerometer reading will be (in world space)
        const Eigen::Vector3f gravity_accel_g_units= -m_identity_gravity_direction;
        const Eigen::Vector3f linear_accel_g_units= x.get_linear_acceleration() * k_ms2_to_g_units;
        const Eigen::Vector3f accel_world= linear_accel_g_units + gravity_accel_g_units;
        const Eigen::Quaternionf accel_world_quat(0.f, accel_world.x(), accel_world.y(), accel_world.z());

        // Put the accelerometer prediction into the local space of the controller
        const Eigen::Vector3f accel_local= orientation*(accel_world_quat*orientation.conjugate()).vec();

        // Use the angular velocity from the state to predict what the gyro reading will be
        const Eigen::Vector3f gyro_local= x.get_angular_velocity(); 

        // Use the orientation from the state to predict
        // what the magnetometer reading should be
        const Eigen::Vector3f &mag_world= m_identity_magnetometer_direction;
        const Eigen::Quaternionf mag_world_quat(0.f, mag_world.x(), mag_world.y(), mag_world.z());
        const Eigen::Vector3f mag_local= orientation*(mag_world_quat*orientation.conjugate()).vec();

        // Save the predictions into the measurement vector
        predicted_measurement.set_accelerometer(accel_local);
        predicted_measurement.set_magnetometer(mag_local);
        predicted_measurement.set_gyroscope(gyro_local);
        predicted_measurement.set_optical_position(position);

        return predicted_measurement;
    }

protected:
    Eigen::Vector3f m_identity_gravity_direction;
    Eigen::Vector3f m_identity_magnetometer_direction;
};

/**
* @brief Measurement model for measuring DS4 controller
*
* This is the measurement model for measuring the position and orientation of the DS4 controller.
* The measurement is given by the optical trackers.
*/
class DS4_MeasurementModel : public Kalman::MeasurementModel<PoseStateVectorf, DS4_MeasurementVectorf, Kalman::SquareRootBase>
{
public:
    void init(const PoseFilterConstants &constants)
    {
        Kalman::Covariance<DS4_MeasurementVectorf> measurement_covariance = 
			Kalman::Covariance<DS4_MeasurementVectorf>::Zero();

        update_measurement_covariance(constants, 0.f, 0.f);

		m_identity_gravity_direction= constants.orientation_constants.gravity_calibration_direction;
    }

	void update_measurement_covariance(
		const PoseFilterConstants &constants,
		const float position_quality,
		const float orientation_quality)
	{
        // Start off using the maximum variance values
        const float position_variance= 
			lerp_clampf(
				constants.position_constants.max_position_variance,
				constants.position_constants.min_position_variance,
				position_quality);
		const float angle_axis_variance=
			lerp_clampf(
				constants.orientation_constants.max_orientation_variance,
				constants.orientation_constants.min_orientation_variance,
				orientation_quality);

        // Update the measurement covariance R
        Kalman::Covariance<DS4_MeasurementVectorf> R = 
			Kalman::Covariance<DS4_MeasurementVectorf>::Zero();
        R(PSMOVE_OPTICAL_POSITION_X, PSMOVE_OPTICAL_POSITION_X) = position_variance;
        R(PSMOVE_OPTICAL_POSITION_Y, PSMOVE_OPTICAL_POSITION_Y) = position_variance;
        R(PSMOVE_OPTICAL_POSITION_Z, PSMOVE_OPTICAL_POSITION_Z) = position_variance;
        R(DS4_OPTICAL_ANGLE_AXIS_X, DS4_OPTICAL_ANGLE_AXIS_X) = angle_axis_variance; 
        R(DS4_OPTICAL_ANGLE_AXIS_Y, DS4_OPTICAL_ANGLE_AXIS_Y) = angle_axis_variance;
        R(DS4_OPTICAL_ANGLE_AXIS_Z, DS4_OPTICAL_ANGLE_AXIS_Z) = angle_axis_variance;
        setCovariance(R);
	}

    /**
    * @brief Definition of (possibly non-linear) measurement function
    *
    * This function maps the system state to the measurement that is expected
    * to be received from the sensor assuming the system is currently in the
    * estimated state.
    *
    * @param [in] x The system state in current time-step
    * @returns The (predicted) sensor measurement for the system state
    */
    DS4_MeasurementVectorf h(const PoseStateVectorf& x) const
    {
        DS4_MeasurementVectorf predicted_measurement;

        // Use the position and orientation from the state for predictions
        const Eigen::Vector3f position= x.get_position();
        const Eigen::Quaternionf orientation= x.get_quaternion();

        // Use the current linear acceleration from the state to predict
        // what the accelerometer reading will be (in world space)
        const Eigen::Vector3f gravity_accel_g_units= -m_identity_gravity_direction;
        const Eigen::Vector3f linear_accel_g_units= x.get_linear_acceleration() * k_ms2_to_g_units;
        const Eigen::Vector3f accel_world= linear_accel_g_units + gravity_accel_g_units;
        const Eigen::Quaternionf accel_world_quat(0.f, accel_world.x(), accel_world.y(), accel_world.z());

        // Put the accelerometer prediction into the local space of the controller
        const Eigen::Vector3f accel_local= orientation*(accel_world_quat*orientation.conjugate()).vec();

        // Use the angular velocity from the state to predict what the gyro reading will be
        const Eigen::Vector3f gyro_local= x.get_angular_velocity(); 

        // Save the predictions into the measurement vector
        predicted_measurement.set_accelerometer(accel_local);
        predicted_measurement.set_gyroscope(gyro_local);
        predicted_measurement.set_optical_position(position);
        predicted_measurement.set_optical_quaternion(orientation);

        return predicted_measurement;
    }

protected:
    Eigen::Vector3f m_identity_gravity_direction;
};

/**
 * @brief Specialized Square Root Unscented Kalman Filter (SR-UKF)
 *
 * @note This is the square-root implementation variant of UnscentedKalmanFilter
 * 
 * This implementation is based upon [The square-root unscented Kalman filter for state and parameter-estimation](http://dx.doi.org/10.1109/ICASSP.2001.940586) by Rudolph van der Merwe and Eric A. Wan.
 * Whenever "the paper" is referenced within this file then this paper is meant.
 *
 * This is a parallel version of Kalman::SquareRootUnscentedKalmanFilter. 
 * Because the state and measurement has a weird rotation type in it we have to do 
 * special work when computing differences, adding deltas, and computing means.
 * 
 * @param StateType The vector-type of the system state (usually some type derived from Kalman::Vector)
 */
namespace Kalman {
	class PoseSRUFK
	{
	public:
		using Control = Vector<float, 0>;

		//! Type of the state vector
		typedef PoseStateVectorf State;

		//! The number of sigma points (depending on state dimensionality)
		static constexpr int SigmaPointCount = 2 * State::RowsAtCompileTime + 1;

        //! Unscented Kalman Filter base type
        typedef UnscentedKalmanFilterBase<PoseStateVectorf> UnscentedBase;
        
		//! Vector containg the sigma scaling weights
		typedef Vector<float, SigmaPointCount> SigmaWeights;

		//! Matrix type containing the sigma state or measurement points
		template<class Type>
		using SigmaPoints = Matrix<float, Type::RowsAtCompileTime, SigmaPointCount>;

		//! Kalman Gain Matrix Type
		template<class Measurement>
		using KalmanGain = Kalman::KalmanGain<State, Measurement>;

    protected:
		//! Estimated state
		State x;

		//! Covariance Square Root
		CovarianceSquareRoot<State> S;

		//! Sigma weights (m)
		SigmaWeights sigmaWeights_m;
		
		//! Sigma weights (c)
		SigmaWeights sigmaWeights_c;

		//! Sigma points (state)
		SigmaPoints<State> sigmaStatePoints;

		// Weight parameters
		float alpha;    //!< Scaling parameter for spread of sigma points (usually \f$ 1E-4 \leq \alpha \leq 1 \f$)
		float beta;     //!< Parameter for prior knowledge about the distribution (\f$ \beta = 2 \f$ is optimal for Gaussian)
		float kappa;    //!< Secondary scaling parameter (usually 0)
		float gamma;    //!< \f$ \gamma = \sqrt{L + \lambda} \f$ with \f$ L \f$ being the state dimensionality
		float lambda;   //!< \f$ \lambda = \alpha^2 ( L + \kappa ) - L\f$ with \f$ L \f$ being the state dimensionality
                     
	public:
		/**
			* Constructor
			* 
			* See paper for detailed parameter explanation
			* 
			* @param alpha Scaling parameter for spread of sigma points (usually \f$ 1E-4 \leq \alpha \leq 1 \f$)
			* @param beta Parameter for prior knowledge about the distribution (\f$ \beta = 2 \f$ is optimal for Gaussian)
			* @param kappa Secondary scaling parameter (usually 0)
			*/
		PoseSRUFK(float _alpha = 1.f, float _beta = 2.f, float _kappa = 0.f)
			: alpha(_alpha)
			, beta(_beta)
			, kappa(_kappa)
		{
			// Setup state and covariance
			x.setZero();

			// Init covariance to identity
			S.setIdentity();

			// Pre-compute all weights
			computeSigmaWeights();
		}

		void init(const State& initialState)
		{
			x = initialState;
		}

		/**
		* @brief Perform filter prediction step using control input \f$u\f$ and corresponding system model
		*
		* @param [in] s The System model
		* @param [in] u The Control input vector
		* @return The updated state estimate
		*/
		const State& predict( const PoseSystemModel& system_model )
		{
			// No control parameters            
			Control u;
			u.setZero();

			// Compute sigma points
			computeSigmaPoints();

			// Pass each sigma point through non-linear state transition function
			// This covers equation (18) of Algorithm 3.1 in the Paper
			for (int point_index = 0; point_index < SigmaPointCount; ++point_index)
			{
				sigmaStatePoints.col(point_index) = system_model.f(sigmaStatePoints.col(point_index), u);
			}

			// Compute predicted state from predicted sigma points
			x = State::computeWeightedStateAverage<SigmaPointCount>(sigmaStatePoints, sigmaWeights_m);
            
			// Compute the Covariance Square root from sigma points and noise covariance
			// This covers equations (20) and (21) of Algorithm 3.1 in the Paper
			{
				const CovarianceSquareRoot<State> noiseCov= system_model.getCovarianceSquareRoot();

				// -- Compute QR decomposition of (transposed) augmented matrix
				// Subtract the x from each sigma point in the right block of the sigmaStatePoints matrix
				// Part of Eqn 19
				Matrix<float, State::RowsAtCompileTime, SigmaPointCount-1> rightSigmaPointsMinusMean= 
					sigmaStatePoints.template rightCols<SigmaPointCount-1>();
				for (int col_index = 0; col_index <= SigmaPointCount-1; ++col_index)
				{
					State sigmaPoint= rightSigmaPointsMinusMean.col(col_index);

					rightSigmaPointsMinusMean.col(col_index)= sigmaPoint.difference(x);
				}

				// Fill in the weighted sigma point portion of the augmented qr input matrix
				// Part of Eqn 19
				Matrix<float, 2*State::RowsAtCompileTime + State::RowsAtCompileTime, State::RowsAtCompileTime> qr_input;
				qr_input.template topRows<2*State::RowsAtCompileTime>() = 
					std::sqrt(this->sigmaWeights_c[1]) * rightSigmaPointsMinusMean.transpose();

				// Fill in the noise portion of the augmented qr input matrix
				// Part of Eqn 19
				qr_input.template bottomRows<State::RowsAtCompileTime>() = noiseCov.matrixU().toDenseMatrix();

				// TODO: Use ColPivHouseholderQR
				Eigen::HouseholderQR<decltype(qr_input)> qr( qr_input );
            
				// Set R matrix as upper triangular square root
				S.setU(qr.matrixQR().template topRightCorner<State::RowsAtCompileTime, State::RowsAtCompileTime>());
            
				// Perform additional rank 1 update
				// TODO: According to the paper (Section 3, "Cholesky factor updating") the update
				//       is defined using the square root of the scalar, however the correct result
				//       is obtained when using the weight directly rather than using the square root
				//       It should be checked whether this is a bug in Eigen or in the Paper
				// T nu = std::copysign( std::sqrt(std::abs(sigmaWeights_c[0])), sigmaWeights_c[0]);
				float nu = this->sigmaWeights_c[0];
				State firstSigmaPoint= sigmaStatePoints.template leftCols<1>();
				S.rankUpdate(firstSigmaPoint.difference(x), nu );
            
				assert(S.info() == Eigen::Success);
			}
            
			// Return predicted state
			return x;
		}

		/**
		 * @brief Perform filter update step using measurement \f$z\f$ and corresponding measurement model
		 *
		 * @param [in] m The Measurement model
		 * @param [in] z The measurement vector
		 * @return The updated state estimate
		 */
		template<class MeasurementModelType, class Measurement>
		const State& update(
			const MeasurementModelType& measurement_model, 
			const Measurement& z )
		{
			SigmaPoints<Measurement> sigmaMeasurementPoints;
            
			// Compute sigma points (using predicted state)
			computeSigmaPoints();
            
			// Predict the expected sigma measurements from predicted sigma states using measurement model
			// This covers equation (23) of Algorithm 3.1 in the Paper
			for (int col_index = 0; col_index < SigmaPointCount; ++col_index)
			{
				sigmaMeasurementPoints.col(col_index) = measurement_model.h(sigmaStatePoints.col(col_index));
			}

			// Predict measurement from sigma measurement points
			// Calls the appropriate function specialization based on measurement type
			Measurement y= Measurement::computeWeightedMeasurementAverage<SigmaPointCount>(sigmaMeasurementPoints, sigmaWeights_m);
           
			// Compute square root innovation covariance
			// This covers equations (23) and (24) of Algorithm 3.1 in the Paper
			CovarianceSquareRoot<Measurement> S_y;
			{
				const CovarianceSquareRoot<Measurement> noiseCov= measurement_model.getCovarianceSquareRoot();

				// -- Compute QR decomposition of (transposed) augmented matrix
				// Subtract the y from each sigma point in the right block of the sigmaMeasurementPoints matrix
				// Part of Eqn 23
				Matrix<float, Measurement::RowsAtCompileTime, SigmaPointCount-1> rightSigmaPointsMinusMean= 
					sigmaMeasurementPoints.template rightCols<SigmaPointCount-1>();
				for (int col_index = 0; col_index <= SigmaPointCount-1; ++col_index)
				{
					Measurement sigmaPoint= rightSigmaPointsMinusMean.col(col_index);

					rightSigmaPointsMinusMean.col(col_index)= sigmaPoint.difference(y);
				}

				// Fill in the weighted sigma point portion of the augmented qr input matrix
				// Part of Eqn 23
				Matrix<float, 2*State::RowsAtCompileTime + Measurement::RowsAtCompileTime, Measurement::RowsAtCompileTime> qr_input;
				qr_input.template topRows<2*State::RowsAtCompileTime>() = 
					std::sqrt(this->sigmaWeights_c[1]) * rightSigmaPointsMinusMean.transpose();

				// Fill in the noise portion of the augmented qr input matrix
				// Part of Eqn 23 
				qr_input.template bottomRows<Measurement::RowsAtCompileTime>() = noiseCov.matrixU().toDenseMatrix();

				// TODO: Use ColPivHouseholderQR
				Eigen::HouseholderQR<decltype(qr_input)> qr( qr_input );
	            
				// Set R matrix as upper triangular square root
				S_y.setU(qr.matrixQR().template topRightCorner<Measurement::RowsAtCompileTime, Measurement::RowsAtCompileTime>());
	            
				// Perform additional rank 1 update
				// TODO: According to the paper (Section 3, "Cholesky factor updating") the update
				//       is defined using the square root of the scalar, however the correct result
				//       is obtained when using the weight directly rather than using the square root
				//       It should be checked whether this is a bug in Eigen or in the Paper
				// T nu = std::copysign( std::sqrt(std::abs(sigmaWeights_c[0])), sigmaWeights_c[0]);
				float nu = this->sigmaWeights_c[0];
				Measurement firstSigmaMeasurementPoint= sigmaMeasurementPoints.template leftCols<1>();
				S_y.rankUpdate( firstSigmaMeasurementPoint.difference(y), nu );
	            
				assert(S_y.info() == Eigen::Success);
			}
            		    
			// Compute the Kalman Gain from predicted measurement and sigma points and the innovation covariance.
			// 
			// This covers equations (27) and (28) of Algorithm 3.1 in the Paper
			//
			// We need to solve the equation \f$ K (S_y S_y^T) = P \f$ for \f$ K \f$ using backsubstitution.
			// In order to apply standard backsubstitution using the `solve` method we must bring the
			// equation into the form
			// \f[ AX = B \qquad \text{with } A = S_yS_y^T \f]
			// Thus we transpose the whole equation to obtain
			// \f{align*}{
			//   ( K (S_yS_y^T))^T &= P^T \Leftrightarrow \\
			//   (S_yS_y^T)^T K^T &= P^T \Leftrightarrow \\
			//   (S_yS_y^T) K^T &= P^T
			//\f}
			// Hence our \f$ X := K^T\f$ and \f$ B:= P^T \f$
			KalmanGain<Measurement> K;
			{
				SigmaPoints<State> sigmaStatePointsMinusX;
				for (int col_index = 0; col_index <= SigmaPoints<State>::ColsAtCompileTime; ++col_index)
				{
					State sigmaPoint= sigmaStatePoints.col(col_index);

					sigmaStatePointsMinusX.col(col_index)= sigmaPoint.difference(x);
				}

				SigmaPoints<Measurement> sigmaMeasurementPointsMinusY;
				for (int col_index = 0; col_index <= SigmaPoints<Measurement>::ColsAtCompileTime; ++col_index)
				{
					Measurement measurement= sigmaMeasurementPoints.col(col_index);

					sigmaMeasurementPointsMinusY.col(col_index)= measurement.difference(y);
				}

				// Note: The intermediate eval() is needed here (for now) due to a bug in Eigen that occurs
				// when Measurement::RowsAtCompileTime == 1 AND State::RowsAtCompileTime >= 8
				decltype(sigmaStatePoints) W = this->sigmaWeights_c.transpose().template replicate<State::RowsAtCompileTime,1>();
				Matrix<float, State::RowsAtCompileTime, Measurement::RowsAtCompileTime> P
						= sigmaStatePointsMinusX.cwiseProduct( W ).eval()
						* sigmaMeasurementPointsMinusY.transpose();
            
				K = S_y.solve(P.transpose()).transpose();
			}
            
			// Update state
			//x += K * ( z - y );
			x= x.addition(K*z.difference(y));
            
			// Update the state covariance matrix using the Kalman Gain and the Innovation Covariance
			// This covers equations (29) and (30) of Algorithm 3.1 in the Paper
			{
				KalmanGain<Measurement> U = K * S_y.matrixL();

				for(int i = 0; i < U.cols(); ++i)
				{
					S.rankUpdate( U.col(i), -1 );
                
					assert( S.info() != Eigen::NumericalIssue );
				}            
			}
            
			return x;
		}

	protected:
		/**
		* @brief Compute sigma weights
		*/
		void computeSigmaWeights()
		{
			const float L = static_cast<float>(State::RowsAtCompileTime);

			lambda = alpha * alpha * (L + kappa) - L;
			gamma = sqrtf(L + lambda);

			// Make sure L != -lambda to avoid division by zero
			assert(fabsf(L + lambda) > 1e-6f);

			// Make sure L != -kappa to avoid division by zero
			assert(fabsf(L + kappa) > 1e-6f);

			float W_m_0 = lambda / (L + lambda);
			float W_c_0 = W_m_0 + (1.f - alpha*alpha + beta);
			float W_i = 1.f / (2.f * alpha*alpha * (L + kappa));

			// Make sure W_i > 0 to avoid square-root of negative number
			assert(W_i > 0.f);

			sigmaWeights_m[0] = W_m_0;
			sigmaWeights_c[0] = W_c_0;

			for (int point_index = 1; point_index < SigmaPointCount; ++point_index)
			{
				sigmaWeights_m[point_index] = W_i;
				sigmaWeights_c[point_index] = W_i;
			}
		}

		/**
		* @brief Compute sigma points from current state estimate and state covariance
		*
		* @note This covers equations (17) and (22) of Algorithm 3.1 in the Paper
		*/
		bool computeSigmaPoints()
		{
			// Get square root of covariance
			Matrix<float, State::RowsAtCompileTime, State::RowsAtCompileTime> _S = S.matrixL().toDenseMatrix();

			// Fill in the first column with the state vector 'x'
			sigmaStatePoints.template leftCols<1>() = x;

			// Apply the state delta column by column
			for (int col_index = 1; col_index <= State::RowsAtCompileTime; ++col_index)
			{
				// Set center block with x + gamma * S
				sigmaStatePoints.col(col_index) = 
					x.addition(this->gamma * _S.col(col_index));

				// Set right block with x - gamma * S
				sigmaStatePoints.col(col_index+State::RowsAtCompileTime) = 
					x.addition(-this->gamma * _S.col(col_index));
			}

			return true;
		}
	};
}

class KalmanFilterImpl
{
public:
    /// Is the current fusion state valid
    bool bIsValid;

    /// Quaternion measured when controller points towards camera 
    Eigen::Quaternionf reset_orientation;

    /// Position that's considered the origin position 
    Eigen::Vector3f origin_position; // meters

    /// All state parameters of the controller
    PoseStateVectorf state_vector;

    /// Used to model how the physics of the controller evolves
    PoseSystemModel system_model;

    /// Unscented Kalman Filter instance
	Kalman::PoseSRUFK ukf;

    KalmanFilterImpl() 
		: ukf(k_ukf_alpha, k_ukf_beta, k_ukf_kappa)
    {
    }

    virtual void init(const PoseFilterConstants &constants)
    {
        bIsValid = false;
        reset_orientation = Eigen::Quaternionf::Identity();
        origin_position = Eigen::Vector3f::Zero();

        state_vector.setZero();

        system_model.init(constants);
        ukf.init(state_vector);
    }
};

class DS4KalmanFilterImpl : public KalmanFilterImpl
{
public:
    DS4_MeasurementModel measurement_model;

	void init(const PoseFilterConstants &constants) override
	{
		KalmanFilterImpl::init(constants);
		measurement_model.init(constants);
	}
};

class PSMoveKalmanFilterImpl : public KalmanFilterImpl
{
public:
    PSMove_MeasurementModel measurement_model;

	void init(const PoseFilterConstants &constants) override
	{
		KalmanFilterImpl::init(constants);
		measurement_model.init(constants);
	}
};

//-- public interface --
//-- KalmanFilterOpticalPoseARG --
KalmanPoseFilter::KalmanPoseFilter() 
    : m_filter(nullptr)
{
    memset(&m_constants, 0, sizeof(PoseFilterConstants));
}

bool KalmanPoseFilter::init(const PoseFilterConstants &constants)
{
    m_constants = constants;

    // cleanup any existing filter
    if (m_filter != nullptr)
    {
        delete m_filter;
        m_filter;
    }

    return true;
}

bool KalmanPoseFilter::getIsStateValid() const
{
    return m_filter->bIsValid;
}

void KalmanPoseFilter::resetState()
{
    m_filter->init(m_constants);
}

void KalmanPoseFilter::recenterState()
{
    Eigen::Quaternionf q_inverse = getOrientation().conjugate();

    eigen_quaternion_normalize_with_default(q_inverse, Eigen::Quaternionf::Identity());
    m_filter->reset_orientation = q_inverse;
    m_filter->origin_position = getPosition();
}

Eigen::Quaternionf KalmanPoseFilter::getOrientation(float time) const
{
    Eigen::Quaternionf result = Eigen::Quaternionf::Identity();

    if (m_filter->bIsValid)
    {
        const Eigen::Quaternionf state_orientation = m_filter->state_vector.get_quaternion();
        Eigen::Quaternionf predicted_orientation = state_orientation;

        if (fabsf(time) > k_real_epsilon)
        {
            const Eigen::Quaternionf &quaternion_derivative =
                eigen_angular_velocity_to_quaternion_derivative(result, getAngularVelocity());

            predicted_orientation = Eigen::Quaternionf(
                state_orientation.coeffs()
                + quaternion_derivative.coeffs()*time).normalized();
        }

        result = m_filter->reset_orientation * predicted_orientation;
    }

    return result;
}

Eigen::Vector3f KalmanPoseFilter::getAngularVelocity() const
{
    return m_filter->state_vector.get_angular_velocity();
}

Eigen::Vector3f KalmanPoseFilter::getAngularAcceleration() const
{
    return Eigen::Vector3f::Zero();
}

Eigen::Vector3f KalmanPoseFilter::getPosition(float time) const
{
    Eigen::Vector3f result = Eigen::Vector3f::Zero();

    if (m_filter->bIsValid)
    {
        Eigen::Vector3f state_position= m_filter->state_vector.get_position();
        Eigen::Vector3f predicted_position =
            is_nearly_zero(time)
            ? state_position
            : state_position + getVelocity() * time;

        result = predicted_position - m_filter->origin_position;
        result = result * k_meters_to_centimeters;
    }

    return result;
}

Eigen::Vector3f KalmanPoseFilter::getVelocity() const
{
    return m_filter->state_vector.get_linear_velocity() * k_meters_to_centimeters;
}

Eigen::Vector3f KalmanPoseFilter::getAcceleration() const
{
    return m_filter->state_vector.get_linear_acceleration() * k_meters_to_centimeters;
}

//-- KalmanFilterOpticalPoseARG --
bool KalmanPoseFilterDS4::init(const PoseFilterConstants &constants)
{
    KalmanPoseFilter::init(constants);

    DS4KalmanFilterImpl *filter = new DS4KalmanFilterImpl();
    filter->init(constants);
    m_filter = filter;

    return true;
}

void KalmanPoseFilterDS4::update(const float delta_time, const PoseFilterPacket &packet)
{
    if (m_filter->bIsValid)
    {
        // Predict state for current time-step using the filters
        m_filter->system_model.set_time_step(delta_time);
        m_filter->state_vector = m_filter->ukf.predict(m_filter->system_model);

        // Get the measurement model for the DS4 from the derived filter impl
        DS4_MeasurementModel &measurement_model = static_cast<DS4KalmanFilterImpl *>(m_filter)->measurement_model;

        // Project the current state onto a predicted measurement as a default
        // in case no observation is available
        DS4_MeasurementVectorf measurement = measurement_model.h(m_filter->state_vector);

        // Accelerometer and gyroscope measurements are always available
        measurement.set_accelerometer(packet.imu_accelerometer);
        measurement.set_gyroscope(packet.imu_gyroscope);

        if (packet.optical_orientation_quality > 0.f || packet.optical_position_quality > 0.f)
        {
			// Adjust the amount we trust the optical measurements based on the quality parameters
            measurement_model.update_measurement_covariance(
				m_constants, 
				packet.optical_position_quality, 
				packet.optical_orientation_quality);

            // If available, use the optical orientation measurement
            if (packet.optical_orientation_quality > 0.f)
            {
                measurement.set_optical_quaternion(packet.optical_orientation);
            }

            // If available, use the optical position
            if (packet.optical_position_quality > 0.f)
            {
                // State internally stores position in meters
                measurement.set_optical_position(packet.optical_position * k_centimeters_to_meters);
            }
        }

        // Update UKF
        m_filter->state_vector = m_filter->ukf.update(measurement_model, measurement);
    }
    else if (packet.optical_position_quality > 0.f)
    {
        m_filter->state_vector.setZero();
        m_filter->state_vector.set_position(packet.optical_position * k_centimeters_to_meters);

        if (packet.optical_position_quality > 0.f)
        {
            m_filter->state_vector.set_quaternion(packet.optical_orientation);
        }
        else
        {
            m_filter->state_vector.set_quaternion(Eigen::Quaternionf::Identity());
        }

        m_filter->bIsValid= true;
    }
}

//-- PSMovePoseKalmanFilter --
bool PSMovePoseKalmanFilter::init(const PoseFilterConstants &constants)
{
    KalmanPoseFilter::init(constants);

    PSMoveKalmanFilterImpl *filter = new PSMoveKalmanFilterImpl();
    filter->init(constants);
    m_filter = filter;

    return true;
}

void PSMovePoseKalmanFilter::update(const float delta_time, const PoseFilterPacket &packet)
{
    if (m_filter->bIsValid)
    {
        // Predict state for current time-step using the filters
        m_filter->state_vector = m_filter->ukf.predict(m_filter->system_model);

        // Get the measurement model for the PSMove from the derived filter impl
        PSMove_MeasurementModel &measurement_model = static_cast<PSMoveKalmanFilterImpl *>(m_filter)->measurement_model;

        // Project the current state onto a predicted measurement as a default
        // in case no observation is available
        PSMove_MeasurementVectorf measurement = measurement_model.h(m_filter->state_vector);

        // Accelerometer, magnetometer and gyroscope measurements are always available
        measurement.set_accelerometer(packet.imu_accelerometer);
        measurement.set_gyroscope(packet.imu_gyroscope);
        measurement.set_magnetometer(packet.imu_magnetometer);

        // If available, use the optical position
        if (packet.optical_position_quality > 0.f)
        {
			// Adjust the amount we trust the optical measurements based on the quality parameters
			measurement_model.update_measurement_covariance(m_constants, packet.optical_position_quality);

			// Assign the latest optical measurement from the packet
            measurement.set_optical_position(packet.optical_position);
        }

        // Update UKF
        m_filter->state_vector = m_filter->ukf.update(measurement_model, measurement);
    }
    else if (packet.optical_position_quality > 0.f)
    {
        m_filter->state_vector.setZero();
        m_filter->state_vector.set_position(packet.optical_position * k_centimeters_to_meters);
        m_filter->state_vector.set_quaternion(Eigen::Quaternionf::Identity());

        m_filter->bIsValid= true;
    }
}

//-- Private functions --
// From: Q_discrete_white_noise in https://github.com/rlabbe/filterpy/blob/master/filterpy/common/discretization.py
template <class StateType>
void Q_discrete_3rd_order_white_noise(
    const float dT,
    const float var,
    const int state_index,
    Kalman::Covariance<StateType> &Q)
{
    const float dT_squared = dT*dT;
    const float q4 = var * dT_squared*dT_squared;
    const float q3 = var * dT_squared*dT;
    const float q2 = var * dT_squared;
    const float q1 = var * dT;
    const float q0 = var;

    // Q = [.5dt^2, dt, 1]*[.5dt^2, dt, 1]^T * variance
    const int &i= state_index;
    Q(i+0,i+0) = 0.25f*q4; Q(i+0,i+1) = 0.5f*q3; Q(i+0,i+2) = 0.5f*q2;
    Q(i+1,i+0) =  0.5f*q3; Q(i+1,i+1) =      q2; Q(i+1,i+2) =      q1;
    Q(i+2,i+0) =  0.5f*q2; Q(i+2,i+1) =      q1; Q(i+2,i+2) =      q0;
}

template <class StateType>
void Q_discrete_2nd_order_white_noise(
	const float dT, 
	const float var, 
	const int state_index, 
	Kalman::Covariance<StateType> &Q)
{
    const float dT_squared = dT*dT;
    const float q4 = var * dT_squared*dT_squared;
    const float q3 = var * dT_squared*dT;
    const float q2 = var * dT_squared;

    // Q = [.5dt^2, dt]*[.5dt^2, dt]^T * variance
    const int &i= state_index;
    Q(i+0,i+0) = 0.25f*q4; Q(i+0,i+1) = 0.5f*q3;
    Q(i+1,i+0) =  0.5f*q3; Q(i+1,i+1) =      q2;
}