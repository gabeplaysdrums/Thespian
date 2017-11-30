#ifndef stats_h_
#define stats_h_

template<class T>
struct Stats
{
    size_t count = 0;
    T average {};
    T stdev {};
    T min {};
    T max {};
};

template<class T>
class StatsAccumulator
{
public:
    void add(T value);
    const Stats<T>& get() const { return data; }
    void reset() { data = {}; }

private:
    Stats<T> data;
};

template<class T>
void StatsAccumulator<T>::add(T value) {
    data.count++;

    if (data.count == 1) {
        data.average = value;
        data.min = value;
        data.max = value;
    }
    else {
        if (value < data.min) {
            data.min = value;
        }

        if (value > data.max) {
            data.max = value;
        }

        const float delta = static_cast<float>(value) - data.average;

        // incremental average:
        // https://math.stackexchange.com/questions/106700/incremental-averageing
        data.average = data.average + delta / data.count;

        // incremental stdev:
        // https://math.stackexchange.com/questions/102978/incremental-computation-of-standard-deviation
        data.stdev = sqrt(
            static_cast<float>(data.count - 2)/(data.count - 1) * data.stdev * data.stdev +
            delta * delta / data.count);
    }
}

#endif // stats_h_