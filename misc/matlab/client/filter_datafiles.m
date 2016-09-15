%% Paths and Constants
addpath(genpath(fullfile(pwd, 'srukf')));

Xdim = 16;  % State vector: posx, velx, accx, posy, vely, accy, posz, velz, accz, qw, qx, qy, qz, avelx, avely, avelz
            % Units: pos: m, vel: m/s, acc: m/s^2, orient. in quat, avel: rad/s
Sdim = 15;  % State covariance. Rotational covariance represented with angle-axis instead of quaternion. See Qdim below.
Odim = 12;  % [a.x, a.y, a.z, g.x, g.y, g.z, m.x, m.y, m.z, pos.x, pos.y, pos.z]
            % Units: acc: g-units, gyro: rad/s, mag: B-units, pos: cm
alpha = 0.6;
beta = 2.0;
kappa = 3 - Xdim;
Qdim = 15;  % Process noise vector: posx, velx, accx, posy, vely, accy, posz, velz, accz, angx, avelx, angy, avely, angz, avelz
            % Units: Same as state vector.
Rdim = Odim;  % Assume observation noise is same dimensionality as obs vec.
q_scale = 1;  % Scale process noise
r_scale = 1;  % Scale observation noise.

%% Load data from csv
datadir = fullfile('..','..','test_data');
trainingdata = csvread(fullfile(datadir, 'stationary.csv'));
testdata = csvread(fullfile(datadir, 'movement.csv'));
%load columns are [a.x, a.y, a.z, g.x, g.y, g.z, m.x, m.y, m.z, pos.x,
%pos.y, pos.z, q.w, q.x, q.y, q.z, time]

% Normalize magnetometer readings.
mag_ix = 7:9;
trainingdata(:, mag_ix) = bsxfun(@rdivide, trainingdata(:, mag_ix), sqrt(sum(trainingdata(:, mag_ix).^2, 2)));
testdata(:, mag_ix) = bsxfun(@rdivide, testdata(:, mag_ix), sqrt(sum(testdata(:, mag_ix).^2, 2)));

%% Initialize filter_struct

% Initialize state
% Assume zero initial velocity / acceleration / angular velocity.
x_init = [...
    0.01*testdata(1,10), 0, 0,...  %p.x, v.x, a.x  (scale position from cm -> m)
    0.01*testdata(1,11), 0, 0,...  %p.y, v.y, a.y
    0.01*testdata(1,12), 0, 0,...  %p.z, v.z, a.z
    testdata(1,[13 14 15 16]),..., %Cheat with estimate from PSMoveService complementary filter. This won't be available usually.
    0, 0, 0]';  % 0 angular velocity.

% TODO: Initial guess at state (sqrt) covariance from trainingdata?
% If yes, remember to scale trainingdata (cm -> m) first.
% State sqrt covariance is required to calculate state sigma points.
% This gets updated on every iteration so initial guess isn't critical.
S_init = zeros(Sdim);
S_init(1:Sdim+1:end) = 0.1;  % Along the diagonal

% Prepare process noise
mean_dt = mean(diff(trainingdata(:, end)));
Q_cov = zeros(Qdim);
Q_3 = q_scale * process_noise(3, mean_dt);
Q_cov(1:3, 1:3) = Q_3;  %Lin-X
Q_cov(4:6, 4:6) = Q_3;  %Lin-Y
Q_cov(7:9, 7:9) = Q_3;  %Lin-Z
% The following is probably ok for angular velocity,
% but probably wrong for orientation
Q_2 = q_scale * process_noise(2, mean_dt);
Q_cov(10:11, 10:11) = Q_2;  % Ang-X
Q_cov(12:13, 12:13) = Q_2;  % Ang-Y
Q_cov(14:15, 14:15) = Q_2;  % Ang-Z
Q_cov(10:15, 10:15) = 0.1*eye(6);  % Overwrite orientation/angvel process noise.
% Q_cov([10 12 14], [10 12 14]) = 0.1;  %Overwrite ax-angle process noise.
Q_init = struct(...
    'dim', Qdim,...
    'mu', zeros(Qdim, 1),...  % Process noise should be zero-mean, I think.
    'cov', chol(Q_cov, 'lower'));  % sqrt matrix
clear Q_cov Q_3 Q_2
% TODO: Check that the axis-angle noise wraps correctly.

% Prepare observation noise from trainingdata
% Assume all observation variables are independent.
R_cov = zeros(Rdim);
R_cov(1:Rdim+1:end) = r_scale * diag(cov(trainingdata(:, 1:Odim)));
R_init = struct(...
    'dim', Rdim,...
    'mu', zeros(Rdim, 1),...
    'cov', sqrt(R_cov));  % Only diagonal so no need to chol.
clear R_cov
R_init.mu(1:3) = [-0.0156696439, 0.0416475534, -0.0112125874];  %accel_bias


% % if doing process noise adaptation, save some variables for later
% ind1 = 1;
% ind2 = Xdim;
% paramdim = Xdim;
% dv = diag(Sv);

filt_struct = struct(...
    'Xdim', Xdim,...  
    'Sdim', Sdim,...
    'Odim', Odim,...  
    'alpha', alpha,...
    'beta', beta,...
    'kappa', kappa,...
    'x', x_init,...
    'S', S_init,...  % Lower-triangular Cholesky factor of state covariance
    'Q', Q_init,...  % Process noise. sqrt expected.
    'R', R_init,...  % Observation noise. sqrt expected.
    'last_obs_time', testdata(1, end) - mean_dt,...
    'intermediates', struct(...
        'X_t', [],...  % Sigma points at time t = k-1
        'X_k', [],...  % Sigma points propagated through process function to time k
        'x_k', [],...  % State estimate = weighted sum of sigma points
        'X_k_r', [],...% Propagated sigma point residuals = (sp - x_k)
        'Sx_k', []),...% Upper-triangular of propagated sp covariance
    'consts', struct(...
        'GRAVITY', 9.806,...  % Approx grav where recordings took place.
        'MAGFIELD', [0.737549126; 0.675293505; 0]));  % Approx mag field where recordings took place.

%% Filter
%Pre-allocate output variables
observations = testdata(:, 1:Odim);
obs_times = testdata(:, end);
predicted_state = nan(size(observations, 1), Xdim);
o_ix = 1;
%%
for o_ix = 1:size(observations, 1)
    %%
    dt = obs_times(o_ix) - filt_struct.last_obs_time;
    filt_struct = srukf_predict(filt_struct, dt);
    filt_struct = srukf_update(filt_struct, observations(o_ix, 1:Odim)');
    filt_struct.last_obs_time = observations(o_ix, end);
    
    %Save predicted state
    predicted_state(o_ix, :) = filt_struct.x;
    
%     % if doing process noise adaptation
%     if isfield(filt_struct.Q, 'adaptMethod')
%         %--- update process noise if needed for joint estimation ----------------------
%         switch filt_struct.Q.adaptMethod
%             case 'anneal'
%                 dv = sqrt(max(filt_struct.Q.adaptParams(1)*(dv.^2) , filt_struct.Q.adaptParams(2)));
%                 Sv(ind1:ind2,ind1:ind2) = diag(dv);
%                 
%             case 'robbins-monro'
%                 nu = 1/filt_struct.Q.adaptParams(1);
%                 subKG = filt_struct.KG(end-paramdim+1:end,:);
%                 dv = sqrt((1-nu)*(dv.^2) + nu*diag(subKG*(subKG*inov*inov')'));
%                 Sv(ind1:ind2,ind1:ind2) = diag(dv);
%                 filt_struct.Q.adaptParams(1) = min(filt_struct.Q.adaptParams(1)+1, filt_struct.Q.adaptParams(2));
%             otherwise
%                 error('Process noise update method not allowed.');     
%         end
%         filt_struct.Q.cov = Sv;
%     end
    
end

%% Test observation function rotations
obs_ix = randi(size(trainingdata, 1));
measured_orientation = trainingdata(obs_ix,[13 14 15 16])';

%trainingdata recorded while controller was motionless upright.

%accelerometer data should be due to gravity only.
% In world space, gravity is [0, -G, 0];
% While the controller is upright, accel due to gravity will be [0, 0, -G].
% While the controller is horizontal, accel due to gravity will be [0, -G,
% 0]
accel = trainingdata(obs_ix, 1:3)';
gravity_world = [0; -1; 0];  %filt_struct.consts.GRAVITY
gravity_cntrl = rotateVector(measured_orientation, gravity_world);
[gravity_cntrl accel]

%magnetometer data should be filt_struct.consts.MAGFIELD rotated to
%controller frame.
mag_identity = filt_struct.consts.MAGFIELD;
magnetometer = trainingdata(obs_ix, 7:9)';

null_quat = [0.5; 0.5; 0.5; -0.5];  % The orientation of the controller when MAGFIELD was measured. Roll +90, Yaw +90
% delta_quat = quaternion_multiply(measured_orientation, quaternion_conjugate(null_quat));
delta_quat = quaternion_multiply(null_quat, quaternion_conjugate(measured_orientation));
mag_cntrl = rotateVector(delta_quat, filt_struct.consts.MAGFIELD);
[mag_cntrl magnetometer]

%% Plot results

figure;

subplot(2,1,1)
pos_est = predicted_state(:, [1 4 7]);
pos_opt = 0.01 * testdata(:, 10:12); %testdata(:, 10:12);
plot3(pos_opt(:, 1), pos_opt(:, 2), pos_opt(:, 3), 'r.', 'MarkerSize', 25)
hold on
plot3(pos_est(:, 1), pos_est(:, 2), pos_est(:, 3), 'b', 'LineWidth', 3)
legend('Raw', 'Filtered')
set(gca, 'Color', 'none')
grid on
box on
axis equal
ax = gca;
ax.BoxStyle = 'full';

subplot(2,1,2)
axang_compl = quat2AxisAngle(testdata(:, 13:16)')';
axang_est = quat2AxisAngle(predicted_state(:, 10:13)')';
plot(axang_est, 'b', 'LineWidth', 3)
hold on
plot(axang_compl, '.', 'MarkerSize', 25)
axis tight
set(gca, 'Color', 'none')