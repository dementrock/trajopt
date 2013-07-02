#include "bsp/bsp.hpp"
#include "car.hpp"
#include <deque>
#include <QApplication>
#include <QtCore>

using namespace BSP;

namespace CarBSP {

CarBSPProblemHelper::CarBSPProblemHelper() : BSPProblemHelper<CarBeliefFunc>() {
	input_dt = 0.25;
	carlen = 0.5;
	goaleps = 0.1;

	set_state_dim(4);
	set_sigma_dof(10);
	set_observe_dim(4);
	set_control_dim(2);

	double state_lbs_array[] = {-10, -5, -PI*1.25, 0};
	double state_ubs_array[] = {10, 5, PI*1.25, 3};
	double control_lbs_array[] = {-PI*0.25, -1};
	double control_ubs_array[] = {PI*0.25, 1.5};

	set_state_bounds(DblVec(state_lbs_array, end(state_lbs_array)), DblVec(state_ubs_array, end(state_ubs_array)));
	set_control_bounds(DblVec(control_lbs_array, end(control_lbs_array)), DblVec(control_ubs_array, end(control_ubs_array)));
	set_variance_cost(VarianceT::Identity(state_dim, state_dim));
	set_final_variance_cost(VarianceT::Identity(state_dim, state_dim)*10);
	set_control_cost(ControlCostT::Identity(control_dim, control_dim)*0.5);
}

void CarBSPProblemHelper::add_goal_constraint(OptProb& prob) {
	for (int i = 0; i < 2; ++i) {
		//prob.addLinearConstraint(exprSub(AffExpr(state_vars.at(T, i)), goal(i)), EQ);

		// box constraint of buffer goaleps around goal position
		Var optvar = state_vars.at(T, i);
		prob.addLinearConstraint(exprSub(AffExpr(optvar), (goal(i)+goaleps) ), INEQ);
		prob.addLinearConstraint(exprSub(exprSub(AffExpr(0), AffExpr(optvar)), (-goal(i)+goaleps) ), INEQ);
	}
}

void CarBSPProblemHelper::init_control_values(vector<ControlT>* output_init_controls) const {
	assert (output_init_controls != NULL);
	output_init_controls->clear();
	output_init_controls->assign(init_controls.begin(), init_controls.end());
	/*
	ControlT control_step = ControlT::Zero(control_dim);// = Control;
	control_step.resize(control_dim);
	control_step(0) = (goal(0) - start(0)) / T;
	control_step(1) = (goal(1) - start(1)) / T;
	for (int i = 0; i < T; ++i) {
		output_init_controls->push_back(control_step);
	}
	 */
}

void CarBSPProblemHelper::RRTplan(bool compute) {

	if (compute) {
		srand(time(0));

		vector<RRTNode> rrtTree;
		RRTNode startNode;
		startNode.x = start;
		rrtTree.push_back(startNode);

		Vector2d poserr = (startNode.x.head<2>() - goal.head<2>());

		double state_lbs_array[] = {-6, -3, -PI, 0};
		double state_ubs_array[] = {-3, 4, PI, 1};
		double control_lbs_array[] = {-PI*0.25, -1};
		double control_ubs_array[] = {PI*0.25, 1};

		int numiter = 0;
		while (poserr.squaredNorm() > goaleps*goaleps || numiter < 100)
		{
			cout << ".";

			StateT sample;
			for (int xd = 0; xd < state_dim; ++xd) {
				sample(xd) = (((double) rand()) / RAND_MAX) * (state_ubs_array[xd] - state_lbs_array[xd]) + state_lbs_array[xd];
			}

			// Check sample for collisions, turned off for now

			int node = -1;
			double mindist = 9e99;
			for (int j = 0; j < (int) rrtTree.size(); ++j) {
				double ddist = (rrtTree[j].x - sample).squaredNorm();
				if (ddist < mindist) {
					mindist = ddist;
					node = j;
				}
			}
			if (node == -1) {
				continue;
			}

			ControlT input;
			for (int ud = 0; ud < control_dim; ++ud) {
				input(ud) = (((double) rand()) / RAND_MAX) * (control_ubs_array[ud] - control_lbs_array[ud]) + control_lbs_array[ud];
			}

			StateNoiseT zero_state_noise = StateNoiseT::Zero(state_noise_dim);
			StateT new_x = state_func->call(rrtTree[node].x, input, zero_state_noise);

			bool valid = true;
			for (int xd = 0; xd < state_dim; ++xd) {
				if (new_x(xd) < state_lbs[xd] || new_x(xd) > state_ubs[xd]) {
					valid = false;
					break;
				}
			}
			if (!valid) {
				continue;
			}

			// Check edge for collisions here, turned off for now

			RRTNode newnode;
			newnode.x = new_x;
			newnode.u = input;
			newnode.bp = node;

			rrtTree.push_back(newnode);
			Vector4d edge;
			edge << rrtTree[node].x(0), rrtTree[node].x(1), newnode.x(0), newnode.x(1);
			rrt_edges.push_back(edge);

			poserr = (newnode.x.head<2>() - goal.head<2>());
			++numiter;
		}
		cout << endl;

		deque<RRTNode> path;

		int i = (int)rrtTree.size() - 1;
		RRTNode node;
		node.x = rrtTree[i].x;
		node.u = ControlT::Zero(control_dim);
		path.push_front(node);

		while (i != 0)
		{
			node.u = rrtTree[i].u;
			i = rrtTree[i].bp;
			node.x = rrtTree[i].x;
			node.bp = -1;

			path.push_front(node);
		}

		init_controls.clear();
		for (int i = 0; i < (int)path.size()-1; ++i) {
			init_controls.push_back(path[i].u);
			cout << path[i].u(0) << " " << path[i].u(1) << endl;
		}

		cout << "T: " << init_controls.size() << endl;
	} else {
		ifstream fptr("/home/sachin/Workspace/bspfov/trajopt/data/car-rrt-seq.txt", ios::in);
		if (!fptr.is_open()) {
			cerr << "Could not open file, check location" << endl;
			std::exit(-1);
		}
		int nu;
		fptr >> nu;
		//cout << "nu: " << nu << endl;
		init_controls.clear();
		ControlT u;
		for (int i = 0; i < nu; ++i) {
			fptr >> u(0) >> u(1);
			init_controls.push_back(u);
			//cout << u(0) << " " << u(1) << endl;
		}
		if (fptr.is_open()) fptr.close();
	}

	//cout << "PAUSED INSIDE RRT BUILD" << endl;
	//int num;
	//cin >> num;
}

CarStateFunc::CarStateFunc() : StateFunc<StateT, ControlT, StateNoiseT>() {}

CarStateFunc::CarStateFunc(BSPProblemHelperBasePtr helper) :
    						StateFunc<StateT, ControlT, StateNoiseT>(helper), car_helper(boost::static_pointer_cast<CarBSPProblemHelper>(helper)) {}

StateT CarStateFunc::operator()(const StateT& x, const ControlT& u, const StateNoiseT& m) const {
	StateT new_x(state_dim);
	/* Euler integration */
	//new_x(0) = x(0) + car_helper->input_dt * x(3) * cos(x(2));
	//new_x(1) = x(1) + car_helper->input_dt * x(3) * sin(x(2));
	//new_x(2) = x(2) + car_helper->input_dt * x(3) * tan(u(0)) / car_helper->carlen;
	//new_x(3) = x(3) + car_helper->input_dt * u(1);
	/* RK4 integration */
	StateT xtmp(state_dim), x1(state_dim), x2(state_dim), x3(state_dim), x4(state_dim);
	xtmp = x;
	x1(0) = xtmp(3) * cos(xtmp(2));
	x1(1) = xtmp(3) * sin(xtmp(2));
	x1(2) = xtmp(3) * tan(u(0)) / car_helper->carlen;
	x1(3) = u(1);
	xtmp = x + 0.5*car_helper->input_dt*x1;
	x2(0) = xtmp(3) * cos(xtmp(2));
	x2(1) = xtmp(3) * sin(xtmp(2));
	x2(2) = xtmp(3) * tan(u(0)) / car_helper->carlen;
	x2(3) = u(1);
	xtmp = x + 0.5*car_helper->input_dt*x2;
	x3(0) = xtmp(3) * cos(xtmp(2));
	x3(1) = xtmp(3) * sin(xtmp(2));
	x3(2) = xtmp(3) * tan(u(0)) / car_helper->carlen;
	x3(3) = u(1);
	xtmp = x + car_helper->input_dt*x3;
	x4(0) = xtmp(3) * cos(xtmp(2));
	x4(1) = xtmp(3) * sin(xtmp(2));
	x4(2) = xtmp(3) * tan(u(0)) / car_helper->carlen;
	x4(3) = u(1);

	new_x = x + car_helper->input_dt/6.0*(x1+2.0*(x2+x3)+x4);

	double umag = u.squaredNorm();
	//return new_x + 0.1*umag*m;
	return new_x + 0.1*m;
}

CarObserveFunc::CarObserveFunc() : ObserveFunc<StateT, ObserveT, ObserveNoiseT>() {}

CarObserveFunc::CarObserveFunc(BSPProblemHelperBasePtr helper) :
    						ObserveFunc<StateT, ObserveT, ObserveNoiseT>(helper), car_helper(boost::static_pointer_cast<CarBSPProblemHelper>(helper)) {}

ObserveT CarObserveFunc::operator()(const StateT& x, const ObserveNoiseT& n) const {
	ObserveT out(observe_dim);
	out(0) = x(0) + 0.01 * n(0);
	out(1) = x(1) + 0.01 * n(1);
	out(2) = x(2) + 0.01 * n(2);
	out(3) = x(3) + 0.01 * n(3);

	//double noise = sqrt((double)(0.5*0.5*x(0)*x(0) + 0.01));
	//out(0) = x(0) + noise*n(0);
	//out(1) = x(1) + noise*n(1);
	//out(2) = x(3) + noise*n(2);
	return out;
}

CarBeliefFunc::CarBeliefFunc() : BeliefFunc<CarStateFunc, CarObserveFunc, BeliefT>(), tol(0.1), alpha(1) {}

CarBeliefFunc::CarBeliefFunc(BSPProblemHelperBasePtr helper, StateFuncPtr f, ObserveFuncPtr h) :
    						tol(0.1), alpha(1), BeliefFunc<CarStateFunc, CarObserveFunc, BeliefT>(helper, f, h), car_helper(boost::static_pointer_cast<CarBSPProblemHelper>(helper)) {}

bool CarBeliefFunc::sgndist(const Vector2d& x, Vector2d* dists) const {
	Vector2d p1; p1 << 0, 2;
	Vector2d p2; p2 << 0, -2;
	(*dists)(0) = (x.head<2>() - p1).norm() - 0.5;
	(*dists)(1) = (x.head<2>() - p2).norm() - 0.5;
	return (*dists)(0) < 0 || (*dists)(1) < 0;
}

ObserveMatT CarBeliefFunc::compute_gamma(const StateT& x) const {
	Vector2d dists;
	sgndist(x.head<2>(), &dists);
	double gamma1 = 1. - (1./(1.+exp(-alpha*(dists(0)+tol))));
	double gamma2 = 1. - (1./(1.+exp(-alpha*(dists(1)+tol))));
	ObserveMatT gamma(observe_dim, observe_dim);
	//gamma << gamma1, 0, 0, gamma2;

	//gamma << gamma1, 0, 0,
	//		 0, gamma2, 0,
	//		 0, 0, 1;

	gamma << gamma1, 0, 0, 0,
			 0, gamma2, 0, 0,
			 0, 0, 1, 0,
			 0, 0, 0, 1;

	return gamma;
}

ObserveMatT CarBeliefFunc::compute_inverse_gamma(const StateT& x) const {
	Vector2d dists;
	sgndist(x.head<2>(), &dists);
	double minval = 1e-4;
	double gamma1 = 1. - (1./(1.+exp(-alpha*(dists(0)+tol))));
	double gamma2 = 1. - (1./(1.+exp(-alpha*(dists(1)+tol))));
	ObserveMatT invgamma(observe_dim, observe_dim);
	gamma1 = std::max(gamma1, minval);
	gamma2 = std::max(gamma2, minval);
	//invgamma << 1/gamma1, 0, 0,
	//		    0, 1/gamma2, 0,
	//		    0, 0, 1;
	//invgamma << 1/gamma1, 0, 0, 1/gamma2;
	invgamma << 1/gamma1, 0, 0, 0,
		        0, 1/gamma2, 0, 0,
		        0, 0, 1, 0,
		        0, 0, 0, 1;

	return invgamma;
}

ObserveStateGradT CarBeliefFunc::sensor_constrained_observe_state_gradient(const ObserveStateGradT& H, const StateT& x) const {
	return compute_gamma(x) * H;
}

ObserveNoiseGradT CarBeliefFunc::sensor_constrained_observe_noise_gradient(const ObserveNoiseGradT& N, const StateT& x) const {
	return N*compute_inverse_gamma(x);
}

VarianceT CarBeliefFunc::sensor_constrained_variance_reduction(const VarianceT& reduction, const StateT& x) const {
	ObserveMatT gamma = compute_gamma(x);
	VarianceT full_gamma = VarianceT::Identity(state_dim, state_dim);
	full_gamma.block(0, 0, observe_dim, observe_dim) = gamma;
	return full_gamma * reduction;
}

CarPlotter::CarPlotter(double x_min, double x_max, double y_min, double y_max, BSPProblemHelperBasePtr helper, QWidget* parent) :
						   BSPQtPlotter(x_min, x_max, y_min, y_max, helper, parent),
						   ProblemState(helper),
						   car_helper(boost::static_pointer_cast<CarBSPProblemHelper>(helper)),
						   old_alpha(-1), cur_alpha(-1), distmap(400, 400, QImage::Format_RGB32) {}

void CarPlotter::paintEvent(QPaintEvent* ) {
	QPainter painter(this);
	if (cur_alpha != old_alpha || distmap.height() != height() || distmap.width() != width()) {
		// replot distmap
		distmap = QImage(width(), height(), QImage::Format_RGB32);
		for (int j = 0; j < height(); ++j) {
			QRgb *line = (QRgb*) distmap.scanLine(j);
			for (int i = 0; i < width(); ++i) {
				double x = unscale_x(i),
						y = unscale_y(j);
				Vector2d pos; pos << x, y;
				Vector2d dists;
				car_helper->belief_func->sgndist(pos, &dists);
				double grayscale = fmax(1./(1. + exp(car_helper->belief_func->alpha*dists(0))),
						1./(1. + exp(car_helper->belief_func->alpha*dists(1))));
				//double grayscale = ((abs(x)-7)*(abs(x)-7))/49;
				line[i] = qRgb(grayscale*255, grayscale*255, grayscale*255);
			}
		}
	}
	painter.drawImage(0, 0, distmap);
	QPen cvx_cov_pen(Qt::red, 3, Qt::SolidLine);
	QPen path_pen(Qt::red, 3, Qt::SolidLine);
	QPen pos_pen(Qt::red, 8, Qt::SolidLine);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::HighQualityAntialiasing);
	painter.setPen(cvx_cov_pen);
	for (int i = 0; i < states_opt.size(); ++i) {
		draw_ellipse(states_opt[i].head<2>(), sigmas_opt[i].topLeftCorner(2,2), painter, 0.5);
	}
	painter.setPen(path_pen);
	for (int i = 0; i < states_opt.size() - 1; ++i) {
		draw_line(states_opt[i](0), states_opt[i](1), states_opt[i+1](0), states_opt[i+1](1), painter);
	}
	vector<Vector4d>& edges = car_helper->rrt_edges;
	for (int i = 0; i < edges.size(); ++i) {
		draw_line(edges[i](0), edges[i](1), edges[i](2), edges[i](3), painter);
	}
	painter.setPen(pos_pen);
	for (int i = 0; i < states_opt.size(); ++i) {
		draw_point(states_opt[i](0), states_opt[i](1), painter);
	}

	// draw beliefs computed using belief dynamics
	QPen cvx_cov_pen2(Qt::blue, 3, Qt::SolidLine);
	painter.setPen(cvx_cov_pen2);
	for (int i = 0; i < states_actual.size(); ++i) {
		draw_ellipse(states_actual[i].head<2>(), sigmas_actual[i].topLeftCorner(2,2), painter, 0.5);
	}
	QPen pos_pen2(Qt::blue, 8, Qt::SolidLine);
	painter.setPen(pos_pen2);
	for (int i = 0; i < states_actual.size(); ++i) {
		draw_point(states_actual[i](0), states_actual[i](1), painter);
	}
}

void CarPlotter::update_plot_data(OptProb*, DblVec& xvec) {
	vector<VectorXd> new_states_opt, new_states_actual;
	vector<MatrixXd> new_sigmas_opt, new_sigmas_actual;
	old_alpha = cur_alpha;
	cur_alpha = car_helper->belief_func->alpha;
	//car_helper->belief_func->alpha = 100;
	BeliefT cur_belief_opt, cur_belief_actual;
	car_helper->belief_func->compose_belief(car_helper->start, car_helper->start_sigma, &cur_belief_actual);
	int T = car_helper->get_T();

	for (int i = 0; i <= T; ++i) {
		StateT cur_state;
		VarianceT cur_sigma;

		cur_belief_opt = (BeliefT) getVec(xvec, car_helper->belief_vars.row(i));
		car_helper->belief_func->extract_state(cur_belief_opt, &cur_state);
		car_helper->belief_func->extract_sigma(cur_belief_opt, &cur_sigma);
		new_states_opt.push_back(cur_state);
		new_sigmas_opt.push_back(cur_sigma);

		car_helper->belief_func->extract_state(cur_belief_actual, &cur_state);
		car_helper->belief_func->extract_sigma(cur_belief_actual, &cur_sigma);
		new_states_actual.push_back(cur_state);
		new_sigmas_actual.push_back(cur_sigma);
		if (i < T) {
			cur_belief_actual = car_helper->belief_func->call(cur_belief_actual, (ControlT) getVec(xvec, car_helper->control_vars.row(i)));
		}
	}
	states_opt = new_states_opt; states_actual = new_states_actual;
	sigmas_opt = new_sigmas_opt; sigmas_actual = new_sigmas_actual;

	/*
	for(int i = 0; i < (int)states_opt.size(); ++i) {
		cout << states_opt[i](0) << " " << states_opt[i](1) << " " << states_opt[i](2) << " " << states_opt[i](3) << endl;
	}

	for(int i = 0; i < (int)states_actual.size(); ++i) {
		cout << states_actual[i](0) << " " << states_actual[i](1) << " " << states_actual[i](2) << " " << states_actual[i](3) << endl;
	}
	*/

	car_helper->belief_func->alpha = cur_alpha;
	this->repaint();
}

CarOptimizerTask::CarOptimizerTask(QObject* parent) : BSPOptimizerTask(parent) {}

CarOptimizerTask::CarOptimizerTask(int argc, char **argv, QObject* parent) : BSPOptimizerTask(argc, argv, parent) {}

void CarOptimizerTask::emit_plot_message(OptProb* prob, DblVec& xvec) {
	BSPOptimizerTask::emit_plot_message(prob, xvec);
}

void CarOptimizerTask::run() {
	bool plotting = false;

	double start_vec_array[] = {-5, 2, -PI*0.5, 0.2};
	double goal_vec_array[] = {-5, -2, 0, 0};

	vector<double> start_vec(start_vec_array, end(start_vec_array));
	vector<double> goal_vec(goal_vec_array, end(goal_vec_array));

	{
		Config config;
		config.add(new Parameter<bool>("plotting", &plotting, "plotting"));
		config.add(new Parameter< vector<double> >("s", &start_vec, "s"));
		config.add(new Parameter< vector<double> >("g", &goal_vec, "g"));
		CommandParser parser(config);
		parser.read(argc, argv, true);
	}

	Vector4d start = toVectorXd(start_vec);
	Vector4d goal = toVectorXd(goal_vec);
	Matrix4d start_sigma = Matrix4d::Identity()*1.5;
	//start_sigma << 1, 0, 0, 0,
	//               0, 1, 0, 0,
	//               0, 0, 1, 0,
	//               0, 0, 0, 1;

	OptProbPtr prob(new OptProb());

	//int T = 30;

	/*
	CarBSPProblemHelperPtr helper(new CarBSPProblemHelper());
	helper->initialize();
	helper->start = start;
	helper->goal = goal;
	helper->start_sigma = start_sigma;
	helper->RRTplan(false);
	helper->T = (int)helper->init_controls.size();
	helper->configure_problem(*prob);

	BSPTrustRegionSQP opt(prob);
	opt.max_iter_ = 100;
	opt.merit_error_coeff_ = 5;
	opt.merit_coeff_increase_ratio_ = 2;
	opt.max_merit_coeff_increases_ = 10;
	opt.trust_shrink_ratio_ = 0.1;
	opt.trust_expand_ratio_ = 1.5;
	opt.min_trust_box_size_ = 0.001;
	opt.min_approx_improve_ = 1e-2;
	opt.min_approx_improve_frac_ = 1e-4;
	opt.improve_ratio_threshold_ = 0.25;
	opt.trust_box_size_ = 1;
	opt.cnt_tolerance_ = 1e-06;
	opt.max_alpha_increases_ = 5;
	*/

	CarBSPProblemHelperPtr helper(new CarBSPProblemHelper());
	helper->initialize();
	helper->start = start;
	helper->goal = goal;
	helper->start_sigma = start_sigma;
	helper->RRTplan(false);
	helper->T = (int)helper->init_controls.size();
	helper->configure_problem(*prob);

	boost::shared_ptr<CarPlotter> plotter;
	if (plotting) {
		double x_min = -7, x_max = 7, y_min = -5, y_max = 5;
		plotter.reset(create_plotter<CarPlotter>(x_min, x_max, y_min, y_max, helper));
		plotter->show();
	}

	for(int i =0; i < 5; ++i)
	{
		BSPTrustRegionSQP opt(prob);
		opt.max_iter_ = 100;
		opt.merit_error_coeff_ = 10;
		opt.merit_coeff_increase_ratio_ = 10;
		opt.max_merit_coeff_increases_ = 3;
		opt.trust_shrink_ratio_ = 0.1;
		opt.trust_expand_ratio_ = 1.5;
		opt.min_trust_box_size_ = 0.001;
		opt.min_approx_improve_ = 1e-2;
		opt.min_approx_improve_frac_ = 1e-4;
		opt.improve_ratio_threshold_ = 0.25;
		opt.trust_box_size_ = 1;
		opt.cnt_tolerance_ = 1e-06;

		helper->configure_optimizer(*prob, opt);

		if (plotting) {
			opt.addCallback(boost::bind(&CarOptimizerTask::emit_plot_message, boost::ref(this), _1, _2));
		}

		opt.optimize();

		DblVec& xvec = opt.x();
		helper->init_controls.clear();
		for (int i = 0; i < helper->T; ++i) {
			ControlT uvec = (ControlT) getVec(xvec, helper->control_vars.row(i));
			//cout << uvec.transpose() << endl;
			helper->init_controls.push_back(uvec);
		}
		helper->belief_func->alpha *= 3;
	}

	//opt.optimize2();

	if (!plotting) {
		emit finished_signal();
	}
}
}

using namespace CarBSP;

int main(int argc, char *argv[]) {
	QApplication app(argc, argv);
	CarOptimizerTask* task = new CarOptimizerTask(argc, argv, &app);
	QTimer::singleShot(0, task, SLOT(run_slot()));
	QObject::connect(task, SIGNAL(finished_signal()), &app, SLOT(quit()));
	return app.exec();
}
