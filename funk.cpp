#include <algorithm>
#include <sqlite3.h>
#include "globals.h"
#include "load.h"
#include "predictor.h"
#include "optimizers.h"
#include <omp.h>

using namespace std;

extern const int MAX_USERS;   // users in the entire training set 
extern const int MAX_MOVIES;  // movies in the entire training set (+1)

static int method = 0; // 0 for sgd, 1 for conjugate gradient
static bool warm_start = 0; // set to 1 to load features.bin, previous solution
static int sample_size = 1000000;
static const int PROBE_SIZE = 1408395;
static const int NON_PROBE_SIZE = 99072112;

Settings parse_args(int argc, char **argv);
void setup(int& num_ratings, int& num_cv_ratings, int& num_test_ratings,
        Data *& ratings, Data *& cv_ratings, Data *& test_ratings);
Data *sample(Data *ratings, int sample_size, int num_ratings);
double cost(Predictor& p, Data *ratings, int num_ratings);
void log(Settings& s, double total_time, double train_cost, double cv_cost, double test_cost);

int main(int argc, char **argv) {
    Settings s = parse_args(argc, argv);
    Predictor p(MAX_USERS, MAX_MOVIES, s.num_features);
    if (warm_start) load_features(p);
    int num_ratings, num_cv_ratings, num_test_ratings;
    Data *ratings, *cv_ratings, *test_ratings;

    setup(num_ratings, num_cv_ratings, num_test_ratings, ratings, cv_ratings, test_ratings);
    cout << "training on " << sample_size << " ratings\n" << endl;

    time_t start,end; time(&start);
    if (method == 0)
        sgd(p, ratings, sample_size, cv_ratings, num_cv_ratings, s);
    else if (method == 1)
        bfgs(p, ratings, sample_size, cv_ratings, num_cv_ratings, s);
    time(&end); 
    
    double total_time, train_cost, cv_cost, test_cost;
    total_time = difftime(end,start);
    train_cost = cost(p, ratings, sample_size);
    cv_cost = cost(p, cv_ratings, num_cv_ratings);
    test_cost = cost(p, test_ratings, num_test_ratings);

    cout << "total time: " << total_time << "s" << endl;
    cout << "training set cost: " << train_cost << endl;
    cout << "cross validation cost: " << cv_cost << endl;
    cout << "test set cost: " << test_cost << endl;

    if (s.dump)
        dump_features(p);

    log(s, total_time, train_cost, cv_cost, test_cost);

    return 0;
}

void setup(int& num_ratings, int& num_cv_ratings, int& num_test_ratings,
        Data *& ratings, Data *& cv_ratings, Data *& test_ratings) {
    ratings = new Data[NON_PROBE_SIZE];
    num_ratings = load_binary(ratings, "cpp/train.bin"); 
    if (sample_size < NON_PROBE_SIZE)
        ratings = sample(ratings, sample_size, num_ratings);
    cv_ratings = new Data[PROBE_SIZE];
    num_cv_ratings = load_binary(cv_ratings, "cpp/cv.bin"); 
    num_test_ratings = num_cv_ratings/2;
    test_ratings = sample(cv_ratings, num_test_ratings, num_cv_ratings);
    num_cv_ratings -= num_test_ratings;
}

// essentially this takes sample_size random ratings and shoves them to the back
// of the array, and returns a pointer to the beginning of this shuffled part
Data *sample(Data *ratings, int sample_size, int num_ratings) {
    int i, r;
    srand(0);
    for (i=0; i<sample_size; i++) {
        r = rand()%num_ratings;
        if (r != num_ratings-i-1)
            swap(ratings[r], ratings[num_ratings-i-1]);
    }
    //cout << "sampled " << sample_size << " ratings" << endl;
    return &ratings[num_ratings - sample_size];
}

Settings parse_args(int argc, char **argv) {
    Settings s;
    if (argc > 3) {
        cout << "usage: ./funk [-i] [x K]" << endl;
        exit(1);
    }
    if (argc == 3) {
        s.num_features = 50;
        sample_size = NON_PROBE_SIZE;
        method = 1;
        s.max_epochs = 0;
        s.K = atof(argv[2]);
        cout << "using K = " << s.K << endl;
    }
    if (argc == 2) { // interactive mode
        cout << "enter number of features: ";
        cin >> s.num_features;
        cout << "enter sample size (0 to use all data): ";
        cin >> sample_size;
        if (sample_size <= 0) 
            sample_size = NON_PROBE_SIZE;
        cout << "enter 0 for sgd, 1 for conjugate gradient: ";
        cin >> method;
        if (method == 0)
            s.lrate = .001;
        else if (method != 1) {
            cout << "only 0 or 1, silly" << endl;
            exit(1);
        }
        if (method == 0) {
            cout << "enter learning rate (0 for " << s.lrate << "): ";
            double temp; cin >> temp;
            if (temp > 0)
                s.lrate = temp;
        }
        cout << "max iterations: ";
        cin >> s.max_epochs;
        cout << "regularizataion parameter (.015): ";
        cin >> s.K;
        cout << "minimum improvement (.0001): ";
        cin >> s.min_improvement;
    }
    return s;
}

void log(Settings& s, double total_time, double train_cost, double cv_cost, double test_cost) {
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc;
    string sql;
    rc = sqlite3_open_v2("log.db", &db, SQLITE_OPEN_READWRITE, NULL);
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    sql = "INSERT INTO log "
          "(datetime, method, num_features, cv_cost, time, train_cost, learning_rate, "
          "regularizer, sample_size, test_cost) "
          "VALUES (datetime('now'), ";
    char temp[100];
    string method_string;
    if (method == 0) method_string = "sgd";
    else if (method == 1) {
        method_string = "cg";
        s.lrate = 0;
    }
    sprintf(temp, "'%s', %d, %f, %f, %f, %f, %f, %d, %f)",
            method_string.c_str(), s.num_features, cv_cost, total_time, train_cost, s.lrate,
            s.K, sample_size, test_cost);
    sql += temp;

    rc = sqlite3_exec(db, sql.c_str(), NULL, 0, &zErrMsg);
    if (rc!=SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
    }
    sqlite3_close(db);

}
