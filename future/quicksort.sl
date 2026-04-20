import std.io;
import std.vector;
import std.random;

void Vector:swap(int i, int j) {
    int tmp = self[i];
    self[i] = self[j];
    self[j] = tmp;
}

int32 main() {
    Vector v;

    for int i in (0..20) {
        v.add(randomInt(1, 100));
    }

    void quickSort(int low, int high) {
        if (low >= high) {
            return;
        }

        int partition(int low, int high) {
            int pivot_val = v[high];
            int i = low - 1;

            for int j in (low..high) {
                if (v[j] <= pivot_val) {
                    i += 1;
                    v.swap(i, j);
                }
            }

            v.swap(i + 1, high);

            return i + 1;
        }

        int pivot = partition(low, high);
        quickSort(low, pivot - 1);
        quickSort(pivot + 1, high);
    }

    quickSort(0, v.size() - 1);

    for int n in v {
        __println(n);
    }

    return 0;
}
