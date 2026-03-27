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

    quickSort(^v, 0, v.size() - 1);

    for int n in v {
        println(n);
    }

    return 0;
}

void quickSort(Vector^ v, int low, int high) {
    if (low >= high) {
        return;
    }

    int pivot = partition(v, low, high);
    quickSort(v, low, pivot - 1);
    quickSort(v, pivot + 1, high);
}

int partition(Vector^ v, int low, int high) {
    int pivot_val = v^[high];
    int i = low - 1;

    for int j in (low..high) {
        if (v^[j] <= pivot_val) {
            i += 1;
            v^.swap(i, j);
        }
    }

    v^.swap(i + 1, high);

    return i + 1;
}
