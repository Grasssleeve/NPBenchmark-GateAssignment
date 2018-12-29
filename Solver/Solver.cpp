#include "Solver.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <mutex>

#include <cmath>


#define CROSS_RATE 80  //mating probility Ϊ 80%
#define MUTATE_RATE 1  //mutattion probility Ϊ 1%
#define POP_SIZE 100
#define N_GENERATIONS 100 //����flight��Ŀ���� 100*flightNum
#define N_COMPARATION 10	//ÿ�ν���ʱѡ��Ĵ���
#define M 10             //����fitenssʱ���Ŵ�Hx��Ҫ��ֵ

using namespace std;


namespace szx {

#pragma region Solver::Cli
int Solver::Cli::run(int argc, char * argv[]) {
    Log(LogSwitch::Szx::Cli) << "parse command line arguments." << endl;
    Set<String> switchSet;
    Map<String, char*> optionMap({ // use string as key to compare string contents instead of pointers.
        { InstancePathOption(), nullptr },
        { SolutionPathOption(), nullptr },
        { RandSeedOption(), nullptr },
        { TimeoutOption(), nullptr },
        { MaxIterOption(), nullptr },
        { JobNumOption(), nullptr },
        { RunIdOption(), nullptr },
        { EnvironmentPathOption(), nullptr },
        { ConfigPathOption(), nullptr },
        { LogPathOption(), nullptr }
    });

    for (int i = 1; i < argc; ++i) { // skip executable name.
        auto mapIter = optionMap.find(argv[i]);
        if (mapIter != optionMap.end()) { // option argument.
            mapIter->second = argv[++i];
        } else { // switch argument.
            switchSet.insert(argv[i]);
        }
    }

    Log(LogSwitch::Szx::Cli) << "execute commands." << endl;
    if (switchSet.find(HelpSwitch()) != switchSet.end()) {
        cout << HelpInfo() << endl;
    }

    if (switchSet.find(AuthorNameSwitch()) != switchSet.end()) {
        cout << AuthorName() << endl;
    }

    Solver::Environment env;
    env.load(optionMap);
    if (env.instPath.empty() || env.slnPath.empty()) { return -1; }

    Solver::Configuration cfg;
    cfg.load(env.cfgPath);

    Log(LogSwitch::Szx::Input) << "load instance " << env.instPath << " (seed=" << env.randSeed << ")." << endl;
    Problem::Input input;
    if (!input.load(env.instPath)) { return -1; }

    Solver solver(input, env, cfg);
    solver.solve();

    pb::Submission submission;
    submission.set_thread(to_string(env.jobNum));
    submission.set_instance(env.friendlyInstName());
    submission.set_duration(to_string(solver.timer.elapsedSeconds()) + "s");

    solver.output.save(env.slnPath, submission);
    #if SZX_DEBUG
    solver.output.save(env.solutionPathWithTime(), submission);
    solver.record();
    #endif // SZX_DEBUG

    return 0;
}
#pragma endregion Solver::Cli

#pragma region Solver::Environment
void Solver::Environment::load(const Map<String, char*> &optionMap) {
    char *str;

    str = optionMap.at(Cli::EnvironmentPathOption());
    if (str != nullptr) { loadWithoutCalibrate(str); }

    str = optionMap.at(Cli::InstancePathOption());
    if (str != nullptr) { instPath = str; }

    str = optionMap.at(Cli::SolutionPathOption());
    if (str != nullptr) { slnPath = str; }

    str = optionMap.at(Cli::RandSeedOption());
    if (str != nullptr) { randSeed = atoi(str); }

    str = optionMap.at(Cli::TimeoutOption());
    if (str != nullptr) { msTimeout = static_cast<Duration>(atof(str) * Timer::MillisecondsPerSecond); }

    str = optionMap.at(Cli::MaxIterOption());
    if (str != nullptr) { maxIter = atoi(str); }

    str = optionMap.at(Cli::JobNumOption());
    if (str != nullptr) { jobNum = atoi(str); }

    str = optionMap.at(Cli::RunIdOption());
    if (str != nullptr) { rid = str; }

    str = optionMap.at(Cli::ConfigPathOption());
    if (str != nullptr) { cfgPath = str; }

    str = optionMap.at(Cli::LogPathOption());
    if (str != nullptr) { logPath = str; }

    calibrate();
}

void Solver::Environment::load(const String &filePath) {
    loadWithoutCalibrate(filePath);
    calibrate();
}

void Solver::Environment::loadWithoutCalibrate(const String &filePath) {
    // EXTEND[szx][8]: load environment from file.
    // EXTEND[szx][8]: check file existence first.
}

void Solver::Environment::save(const String &filePath) const {
    // EXTEND[szx][8]: save environment to file.
}
void Solver::Environment::calibrate() {
    // adjust thread number.
    int threadNum = thread::hardware_concurrency();
    if ((jobNum <= 0) || (jobNum > threadNum)) { jobNum = threadNum; }

    // adjust timeout.
    msTimeout -= Environment::SaveSolutionTimeInMillisecond;
}
#pragma endregion Solver::Environment

#pragma region Solver::Configuration
void Solver::Configuration::load(const String &filePath) {
    // EXTEND[szx][5]: load configuration from file.
    // EXTEND[szx][8]: check file existence first.
}

void Solver::Configuration::save(const String &filePath) const {
    // EXTEND[szx][5]: save configuration to file.
}
#pragma endregion Solver::Configuration

#pragma region Solver
bool Solver::solve() {
    init();

    int workerNum = (max)(1, env.jobNum / cfg.threadNumPerWorker);
    cfg.threadNumPerWorker = env.jobNum / workerNum;
    List<Solution> solutions(workerNum, Solution(this));
    List<bool> success(workerNum);

    Log(LogSwitch::Szx::Framework) << "launch " << workerNum << " workers." << endl;
    List<thread> threadList;
    threadList.reserve(workerNum);
    for (int i = 0; i < workerNum; ++i) {
        // TODO[szx][2]: as *this is captured by ref, the solver should support concurrency itself, i.e., data members should be read-only or independent for each worker.
        // OPTIMIZE[szx][3]: add a list to specify a series of algorithm to be used by each threads in sequence.
        threadList.emplace_back([&, i]() { success[i] = optimize(solutions[i], i); });
    }
    for (int i = 0; i < workerNum; ++i) { threadList.at(i).join(); }

    Log(LogSwitch::Szx::Framework) << "collect best result among all workers." << endl;
    int bestIndex = -1;
    Length bestValue = 0;
    for (int i = 0; i < workerNum; ++i) {
        if (!success[i]) { continue; }
        Log(LogSwitch::Szx::Framework) << "worker " << i << " got " << solutions[i].flightNumOnBridge << endl;
        if (solutions[i].flightNumOnBridge <= bestValue) { continue; }
        bestIndex = i;
        bestValue = solutions[i].flightNumOnBridge;
    }

    env.rid = to_string(bestIndex);
    if (bestIndex < 0) { return false; }
    output = solutions[bestIndex];
    return true;
}

void Solver::record() const {
    #if SZX_DEBUG
    int generation = 0;

    ostringstream log;

    System::MemoryUsage mu = System::peakMemoryUsage();

    Length obj = output.flightNumOnBridge;
    Length checkerObj = -1;
    bool feasible = check(checkerObj);

    // record basic information.
    log << env.friendlyLocalTime() << ","
        << env.rid << ","
        << env.instPath << ","
        << feasible << "," << (obj - checkerObj) << ","
        << output.flightNumOnBridge << ","
        << timer.elapsedSeconds() << ","
        << mu.physicalMemory << "," << mu.virtualMemory << ","
        << env.randSeed << ","
        << cfg.toBriefStr() << ","
        << generation << "," << iteration << ","
        << (100.0 * output.flightNumOnBridge / input.flights().size()) << "%,";

    // record solution vector.
    // EXTEND[szx][2]: save solution in log.
    log << endl;

    // append all text atomically.
    static mutex logFileMutex;
    lock_guard<mutex> logFileGuard(logFileMutex);

    ofstream logFile(env.logPath, ios::app);
    logFile.seekp(0, ios::end);
    if (logFile.tellp() <= 0) {
        logFile << "Time,ID,Instance,Feasible,ObjMatch,Width,Duration,PhysMem,VirtMem,RandSeed,Config,Generation,Iteration,Ratio,Solution" << endl;
    }
    logFile << log.str();
    logFile.close();
    #endif // SZX_DEBUG
}

bool Solver::check(Length &checkerObj) const {
    #if SZX_DEBUG
    enum CheckerFlag {
        IoError = 0x0,
        FormatError = 0x1,
        FlightNotAssignedError = 0x2,
        IncompatibleAssignmentError = 0x4,
        FlightOverlapError = 0x8
    };

    checkerObj = System::exec("Checker.exe " + env.instPath + " " + env.solutionPathWithTime());
    if (checkerObj > 0) { return true; }
    checkerObj = ~checkerObj;
    if (checkerObj == CheckerFlag::IoError) { Log(LogSwitch::Checker) << "IoError." << endl; }
    if (checkerObj & CheckerFlag::FormatError) { Log(LogSwitch::Checker) << "FormatError." << endl; }
    if (checkerObj & CheckerFlag::FlightNotAssignedError) { Log(LogSwitch::Checker) << "FlightNotAssignedError." << endl; }
    if (checkerObj & CheckerFlag::IncompatibleAssignmentError) { Log(LogSwitch::Checker) << "IncompatibleAssignmentError." << endl; }
    if (checkerObj & CheckerFlag::FlightOverlapError) { Log(LogSwitch::Checker) << "FlightOverlapError." << endl; }
    return false;
    #else
    checkerObj = 0;
    return true;
    #endif // SZX_DEBUG
}

void Solver::init() {
    aux.isCompatible.resize(input.flights().size(), List<bool>(input.airport().gates().size(), true));
    ID f = 0;
    for (auto flight = input.flights().begin(); flight != input.flights().end(); ++flight, ++f) {
        for (auto ig = flight->incompatiblegates().begin(); ig != flight->incompatiblegates().end(); ++ig) {
            aux.isCompatible[f][*ig] = false;
        }
    }
}

bool Solver::optimize(Solution &sln, ID workerId) {
    Log(LogSwitch::Szx::Framework) << "worker " << workerId << " starts." << endl;

    ID gateNum = input.airport().gates().size();
    ID bridgeNum = input.airport().bridgenum();
    ID flightNum = input.flights().size();

    // reset solution state.
    bool status = true;
    auto &assignments(*sln.mutable_assignments());
    assignments.Resize(flightNum, Problem::InvalidId);
    sln.flightNumOnBridge = 0;


    // TODO[0]: replace the following random assignment with your own algorithm.
    //for (ID f = 0; !timer.isTimeOut() && (f < flightNum); ++f) {
    //    assignments[f] = rand.pick(gateNum);
    //    if (assignments[f] < bridgeNum) { ++sln.flightNumOnBridge; } // record obj.
    //}

	this->gateNum = gateNum;
	this->flightNum = flightNum;
	this->bridgeNum = bridgeNum;

	int pop_idx = run_mga();


	for (ID f = 0;  f < flightNum; ++f) {
		assignments[f] = pop[pop_idx][f];
		if (assignments[f] < bridgeNum) { ++sln.flightNumOnBridge; } // record obj.
	}

	delete_malloc();
	/////////////////////////////////////////////////////////////////////////////////////

    Log(LogSwitch::Szx::Framework) << "worker " << workerId << " ends." << endl;
    return status;
}
#pragma endregion Solver

}


// ��ʼ�� int** pop , int* comflictFlights, int* comflictNum
int szx::Solver::init_malloc()
{
	pop = new int*[POP_SIZE];
	for (size_t i = 0; i < POP_SIZE; i++) {
		pop[i] = new int[flightNum];
	}

	conflictFlights = new int*[flightNum];
	for (size_t i = 0; i < flightNum; i++) {
		conflictFlights[i] = new int[flightNum - 1];
		memset(conflictFlights[i], 0, sizeof(int)*(flightNum - 1));
	}

	conflictNum = new int[flightNum];
	memset(conflictNum, 0, sizeof(int)*flightNum);
	return 0;
}


// �ͷ� int** pop , int* comflictFlights, int* comflictNum
int szx::Solver::delete_malloc()
{
	for (size_t i = 0; i < POP_SIZE; i++) {
		delete[] pop[i];
	}
	delete[] pop;

	for (size_t i = 0; i < flightNum; i++) {
		delete[] conflictFlights[i];
	}
	delete[] conflictFlights;

	delete[] conflictNum;
	return 0;
}


// ��ʼ������ int** comflictFlights , int* conflictNum
int szx::Solver::init_conflict_flights()
{
	int* f_beginTime_sort = new int[flightNum];
	for (int i = 0; i < flightNum; i++)
	{
		f_beginTime_sort[i] = i;
	}

	//�������ʱ������
	size_t max = flightNum - 1;
	for (size_t i = 0; i < max; i++)
	{
		for (size_t j = 0; j < max-i; j++)
		{
			int time1 = input.flights(f_beginTime_sort[j]).turnaround().begin();
			int time2= input.flights(f_beginTime_sort[j+1]).turnaround().begin();
			if (time1 > time2)
			{
				swap(f_beginTime_sort[j], f_beginTime_sort[j + 1]);
			}
		}
	}


	for (size_t i = 0; i < flightNum - 1; i++)
	{
		int f_first = f_beginTime_sort[i];
		int f_first_endTime = input.flights(f_first).turnaround().end() + 30; //����30���ӵļ��
		for (size_t j = i + 1; j < flightNum; j++)
		{
			int f_second = f_beginTime_sort[j];			
			int f_second_beginTime = input.flights(f_second).turnaround().begin();
			if (f_first_endTime > f_second_beginTime)
			{
				conflictFlights[f_second][conflictNum[f_second]] = f_first;
				conflictFlights[f_first][conflictNum[f_first]] = f_second;

				conflictNum[f_first]++;
				conflictNum[f_second]++;
			}
		}
	}

	delete[] f_beginTime_sort;
	return 0;
}


// ��ʼ����Ⱥ
int szx::Solver::init_pop()
{
	for (size_t i = 0; i < POP_SIZE; i++) {
		int* h_pop = pop[i];
		for (size_t j = 0; j < flightNum; j++) {
			int gate;
			//		set<int> gate_incompatible = *(incompatibleGates + j);
			//		while (gate_incompatible.count(gate = rand() % gateNum));

			while (true)
			{
				gate = rand.pick(gateNum);
				if (aux.isCompatible[j][gate])
				{
					break;
				}
			}
			h_pop[j] = gate;
		}
	}
	return 0;
}


// //����Ӧ�ȵĺ��� ԽСԽ�� ������Ӧ�ȵ�ֵ
int szx::Solver::get_fitness(int individual_idx)
{

	int* individual = pop[individual_idx];
	//�ȼ���FX����Զ��λ����
	int Fx = 0;
	for (size_t i = 0; i < flightNum; i++)
	{
		if (individual[i] >= bridgeNum)
		{
			Fx++;
		}
	}

	//�ټ���HX������ͻ�ķɻ�
	int Hx = 0;
	for (size_t i = 0; i < flightNum; i++)
	{
		int hx = 0;
		int gate = individual[i];
		int* h_conflict_flights = conflictFlights[i];
		for (size_t j = 0; j < conflictNum[i]; j++)
		{
			int conflictFlight = h_conflict_flights[j];
			int gate_comflictFlight = individual[conflictFlight];  //��ͻ�ķɻ�
			if (gate == gate_comflictFlight)
			{
				hx++;
			}
		}

		Hx += hx * hx;  //hxƽ�������
	}

	int sum = 0;
	sum = Fx + M * (flightNum - bridgeNum)*Hx;  //��֤Hx��Զ��λ��Ӱ���
	return sum; //���ؼ����ֵ
}



// ����
int szx::Solver::mutate(int loser_idx)
{
	int* loser = pop[loser_idx];
	bool* mutation_idx = new bool[flightNum];
	for (size_t i = 0; i < flightNum; i++)
	{
		if (rand.isPicked(MUTATE_RATE, 100))
		{
			mutation_idx[i] = true;
		}
		else
		{
			mutation_idx[i] = false;
		}
	}

	for (size_t i = 0; i < flightNum; i++)
	{
		if (mutation_idx[i])
		{
			int gate;
			//		set<int> gate_incompatible = *(incompatibleGates + i);
			//		while (gate_incompatible.count(gate = rand() % gateNum));

			while (true)
			{
				gate = rand.pick(gateNum);
				if (aux.isCompatible[i][gate])
				{
					break;
				}
			}

			loser[i] = gate;
		}
	}

	delete[] mutation_idx;
	return 0;
}


// �ӽ�loser��winner
int szx::Solver::crossover(int* loser_winner_idx)
{
	bool* cross_idx = new bool[flightNum];

	for (size_t i = 0; i < flightNum; i++) {
		if (rand.isPicked(CROSS_RATE, 100)) {
			cross_idx[i] = true;
		}
		else {
			cross_idx[i] = false;
		}
	}


	int* loser = pop[loser_winner_idx[0]];
	int* winner = pop[loser_winner_idx[1]];

	for (size_t i = 0; i < flightNum; i++) {
		if (cross_idx[i]) {
			loser[i] = winner[i];
		}
	}

	delete[] cross_idx;
	return 0;
}


// ����best_fitness��ֵ
int szx::Solver::envolve(int& winner_idx)
{
	int fitness[2];
	for (size_t i = 0; i < N_COMPARATION; i++)
	{

		//��֤loser��winner��ͬ

		int loser_winner_idx[2];
		loser_winner_idx[0]= rand.pick(POP_SIZE);
		while (true)
		{
			loser_winner_idx[1] = rand.pick(POP_SIZE);
			if (loser_winner_idx[1] != loser_winner_idx[0])
			{
				break;
			}
		}

		
		fitness[0] = get_fitness(loser_winner_idx[0]);
		fitness[1] = get_fitness(loser_winner_idx[1]);

		if (fitness[0]< fitness[1])//fitnessԽСԽ��,
		{
			swap(fitness[0], fitness[1]);
			swap(loser_winner_idx[0], loser_winner_idx[1]);
		}


		winner_idx = loser_winner_idx[1];

		if (fitness[1]==0)
		{
			return fitness[1];

		}

		crossover( loser_winner_idx);

		//loser����
		mutate(loser_winner_idx[0]);

	}


	return fitness[1];
}


int szx::Solver::run_mga()
{
	init_malloc();
	init_pop();
	init_conflict_flights();
	int winner_idx = 0;
	int generation = 0;
	int best_fitness;
	do
	{
		best_fitness=envolve(winner_idx);
		generation++;

		//cout <<"best_fitness="<< best_fitness << endl;
		//cout << "generation=" << generation << endl;
	} while (best_fitness!=0  && (generation<N_GENERATIONS*flightNum || best_fitness>flightNum ));

	//while (best_fitness!=0 && !timer.isTimeOut() && (generation<N_GENERATIONS || best_fitness>flightNum ));
	//cout <<"best_fitness="<< best_fitness << endl;
	return winner_idx;
}
