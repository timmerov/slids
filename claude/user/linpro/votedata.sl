/*
Copyright (C) 2012-2026 tim cotter. All rights reserved.
*/

/*
provide voter bloc data.

data from guthrie voting simulated electorates at end of file.
*/

import map;
import vector;
import votedata;
alias slids;

BlocMap() {
    void insert(char[] list, int size) {
        Vector<int> rankings;
        while() {
            ch = list++^;
            if (ch == 0) {
                break;
            }
            rankings.append(ch - 'A');
        }
        insert(rankings, size);
    }
}

slids:String to_string(
    Rankings^ rankings
) {
    String s;
    for (i : rankings^) {
        ch char = 'A' + i;
        s += ch;
    }
    return s;
}

/* return a bloc map of the voter data. */
BlocMap get_voter_data() {

    /* data set #1 */
    BlocMap bloc_map;
    bloc_map.insert("ABCD", 216);
    bloc_map.insert("BACD", 170);
    bloc_map.insert("BCAD", 22);
    bloc_map.insert("BCDA", 17);
    bloc_map.insert("CBAD", 8);
    bloc_map.insert("CBDA", 19);
    bloc_map.insert("CDBA", 232);
    bloc_map.insert("DCBA", 316);

    return bloc_map;
}


/*
data produced by guthrie voting test harness.

Voter blocs (8):
 ABCD size: 216 utilities: 195.609 163.855 124.604 89.4898 n: 432 approvals: 216 216 0 0
 BACD size: 170 utilities: 154.763 138.645 109.374 89.4801 n: 338 approvals: 170 168 0 0
 BCAD size: 22 utilities: 19.5337 17.6246 16.7973 15.1952 n: 44 approvals: 22 18 4 0
 BCDA size: 17 utilities: 14.1475 13.6301 12.5527 11.703 n: 36 approvals: 17 17 2 0
 CBAD size: 8 utilities: 7.05335 6.59652 6.06454 5.84099 n: 16 approvals: 8 8 0 0
 CBDA size: 19 utilities: 16.298 15.3126 14.6131 13.1389 n: 40 approvals: 19 15 6 0
 CDBA size: 232 utilities: 217.909 197.956 159.491 142.39 n: 463 approvals: 232 225 6 0
 DCBA size: 316 utilities: 280.422 243.915 169.033 139.031 n: 632 approvals: 316 316 0 0

Voter blocs (8):
 ABCD size: 309 utilities: 275.937 165.399 152.234 127.911 n: 315 approvals: 309 6 0 0
 BCAD size: 1 utilities: 0.825546 0.78108 0.734979 0.684008 n: 2 approvals: 1 1 0 0
 BCDA size: 276 utilities: 263.099 253.216 219.331 161.954 n: 618 approvals: 276 276 66 0
 BDCA size: 5 utilities: 4.5206 4.39092 4.32939 2.83802 n: 15 approvals: 5 5 5 0
 CBDA size: 186 utilities: 173.922 167.922 150.953 87.9074 n: 510 approvals: 186 186 138 0
 CDBA size: 16 utilities: 13.1163 12.7172 12.4836 5.51717 n: 48 approvals: 16 16 16 0
 DBCA size: 151 utilities: 135.371 108.484 107.191 58.1811 n: 453 approvals: 151 151 151 0
 DCBA size: 56 utilities: 47.2587 38.9747 38.5999 17.7757 n: 168 approvals: 56 56 56 0

Voter blocs (10):
 ABCD size: 236 utilities: 215.535 206.77 193.306 102.144 n: 708 approvals: 236 236 236 0
 ACBD size: 46 utilities: 42.2661 40.3554 39.0058 21.2701 n: 138 approvals: 46 46 46 0
 BACD size: 247 utilities: 230.778 218.163 188.695 118.98 n: 597 approvals: 247 247 103 0
 BADC size: 1 utilities: 0.76584 0.742987 0.72774 0.712601 n: 2 approvals: 1 1 0 0
 CABD size: 232 utilities: 218.036 198.104 181.163 124.317 n: 580 approvals: 232 232 116 0
 CDAB size: 3 utilities: 2.34939 2.24276 2.21288 2.13977 n: 5 approvals: 3 2 0 0
 DBAC size: 2 utilities: 1.57466 1.40189 1.36245 1.32915 n: 3 approvals: 2 1 0 0
 DBCA size: 1 utilities: 0.815076 0.37118 0.365587 0.35475 n: 1 approvals: 1 0 0 0
 DCAB size: 188 utilities: 169.093 105.735 97.6019 95.8364 n: 200 approvals: 188 12 0 0
 DCBA size: 44 utilities: 38.3373 18.3721 17.1415 17.0157 n: 44 approvals: 44 0 0 0

Voter blocs (13):
 ABCD size: 261 utilities: 206.724 176.729 165.134 126.058 n: 650 approvals: 261 261 128 0
 ABDC size: 13 utilities: 11.1356 10.6948 10.0428 9.59673 n: 26 approvals: 13 13 0 0
 ACBD size: 6 utilities: 4.03682 3.7778 3.76576 2.26645 n: 18 approvals: 6 6 6 0
 BACD size: 18 utilities: 16.3095 15.675 15.0723 12.9523 n: 47 approvals: 18 18 11 0
 BADC size: 8 utilities: 6.814 6.69029 6.44934 6.15899 n: 19 approvals: 8 8 3 0
 BCAD size: 217 utilities: 208.958 197.287 177.465 158.2 n: 468 approvals: 217 217 34 0
 BCDA size: 77 utilities: 71.2826 69.0696 61.8273 58.1505 n: 162 approvals: 77 76 9 0
 BDAC size: 8 utilities: 6.88324 6.66745 6.47745 6.26937 n: 16 approvals: 8 7 1 0
 BDCA size: 3 utilities: 2.65009 2.52033 2.43426 2.39856 n: 5 approvals: 3 2 0 0
 CBAD size: 132 utilities: 125.447 116.403 95.7662 88.4669 n: 267 approvals: 132 132 3 0
 CBDA size: 104 utilities: 95.7292 89.8799 76.1901 70.8709 n: 210 approvals: 104 104 2 0
 DBAC size: 13 utilities: 11.1959 10.6028 10.0297 9.66766 n: 26 approvals: 13 12 1 0
 DBCA size: 140 utilities: 128.894 97.4958 91.6472 84.856 n: 156 approvals: 140 12 4 0

Voter blocs (8):
 ABCD size: 272 utilities: 224.285 191.659 163.158 139.497 n: 544 approvals: 272 272 0 0
 BACD size: 41 utilities: 37.3031 35.3725 33.5425 29.3159 n: 93 approvals: 41 41 11 0
 BCAD size: 121 utilities: 112.767 105.554 96.4154 88.6471 n: 248 approvals: 121 121 6 0
 BCDA size: 27 utilities: 25.1186 24.5657 22.1016 21.4519 n: 57 approvals: 27 27 3 0
 CBAD size: 1 utilities: 0.887541 0.88391 0.730138 0.727046 n: 2 approvals: 1 1 0 0
 CBDA size: 137 utilities: 130.608 122.607 112.333 100.48 n: 290 approvals: 137 137 16 0
 CDBA size: 28 utilities: 26.1606 24.6135 23.6229 19.3848 n: 77 approvals: 28 28 21 0
 DCBA size: 373 utilities: 351.056 308.058 279.63 233.748 n: 761 approvals: 373 373 15 0

Voter blocs (11):
 ABCD size: 310 utilities: 268.47 203.89 169.703 140.981 n: 616 approvals: 310 299 7 0
 BACD size: 35 utilities: 32.4275 27.8551 25.9286 24.542 n: 53 approvals: 35 18 0 0
 BCAD size: 12 utilities: 11.4172 9.42037 9.13825 8.92437 n: 15 approvals: 12 2 1 0
 BCDA size: 75 utilities: 73.1321 59.3885 58.3177 54.1862 n: 82 approvals: 75 4 3 0
 BDCA size: 103 utilities: 95.7833 84.2777 81.2785 68.1976 n: 195 approvals: 103 65 27 0
 CABD size: 1 utilities: 0.805526 0.759967 0.751444 0.663397 n: 3 approvals: 1 1 1 0
 CBAD size: 3 utilities: 2.49646 2.34166 2.25239 2.08835 n: 6 approvals: 3 2 1 0
 CBDA size: 17 utilities: 15.3751 13.3209 12.891 11.6997 n: 26 approvals: 17 9 0 0
 CDBA size: 206 utilities: 196.25 173.35 155.351 121.321 n: 408 approvals: 206 198 4 0
 DBCA size: 67 utilities: 60.3959 55.4682 53.3834 37.5828 n: 201 approvals: 67 67 67 0
 DCBA size: 171 utilities: 157.92 136.753 123.589 81.0926 n: 394 approvals: 171 171 52 0

Voter blocs (7):
 ABCD size: 206 utilities: 181.497 120.227 98.817 75.5633 n: 279 approvals: 206 73 0 0
 BACD size: 13 utilities: 9.96371 9.49506 8.93683 7.43798 n: 29 approvals: 13 13 3 0
 BCAD size: 15 utilities: 13.484 12.0416 10.9477 10.3434 n: 30 approvals: 15 15 0 0
 BCDA size: 138 utilities: 133.031 123.705 107.88 90.5378 n: 276 approvals: 138 138 0 0
 CBDA size: 88 utilities: 84.7551 80.0827 74.8621 50.7528 n: 257 approvals: 88 88 81 0
 CDBA size: 27 utilities: 25.4881 23.9779 23.0241 14.0925 n: 81 approvals: 27 27 27 0
 DCBA size: 513 utilities: 475.128 423.638 371.316 199.867 n: 1539 approvals: 513 513 513 0

Voter blocs (16):
 ABCD size: 132 utilities: 124.85 106.124 101.266 89.9034 n: 206 approvals: 132 71 3 0
 ACBD size: 50 utilities: 45.8212 41.3937 39.7791 36.0164 n: 83 approvals: 50 31 2 0
 BACD size: 163 utilities: 153.763 139.466 129.476 126.986 n: 267 approvals: 163 104 0 0
 BADC size: 42 utilities: 40.3861 34.735 33.2146 32.9991 n: 52 approvals: 42 10 0 0
 BCAD size: 19 utilities: 17.8168 16.6497 16.3427 15.9832 n: 30 approvals: 19 9 2 0
 BCDA size: 37 utilities: 34.6715 32.6475 32.1452 30.8242 n: 68 approvals: 37 22 9 0
 BDAC size: 10 utilities: 9.77185 8.25002 8.16921 8.1033 n: 10 approvals: 10 0 0 0
 BDCA size: 38 utilities: 36.2548 32.7797 32.1357 30.7894 n: 56 approvals: 38 13 5 0
 CABD size: 10 utilities: 9.22618 8.59113 8.39924 8.15969 n: 14 approvals: 10 4 0 0
 CADB size: 2 utilities: 1.81612 1.61837 1.56503 1.53721 n: 3 approvals: 2 1 0 0
 CBAD size: 18 utilities: 16.6049 15.8522 15.5277 15.1545 n: 30 approvals: 18 10 2 0
 CBDA size: 24 utilities: 22.4319 21.1445 20.7959 20.1067 n: 41 approvals: 24 13 4 0
 CDAB size: 47 utilities: 26.5952 24.3084 18.6468 18.4005 n: 90 approvals: 47 43 0 0
 CDBA size: 202 utilities: 175.939 161.74 145.976 139.86 n: 367 approvals: 202 160 5 0
 DBCA size: 8 utilities: 7.50827 6.85807 6.74887 5.78067 n: 23 approvals: 8 8 7 0
 DCBA size: 198 utilities: 171.104 153.918 139.268 118.751 n: 404 approvals: 198 198 8 0

Voter blocs (13):
 ABCD size: 214 utilities: 196.545 163.45 154.726 147.888 n: 277 approvals: 214 63 0 0
 ACBD size: 39 utilities: 37.9702 30.5807 30.2053 29.5775 n: 39 approvals: 39 0 0 0
 ACDB size: 34 utilities: 32.0526 27.4236 26.7642 25.9258 n: 43 approvals: 34 6 3 0
 BACD size: 113 utilities: 102.667 91.6471 86.1829 80.8874 n: 187 approvals: 113 74 0 0
 BCAD size: 42 utilities: 40.1069 34.5325 33.7286 32.5125 n: 45 approvals: 42 3 0 0
 BCDA size: 16 utilities: 15.0229 13.9071 13.1333 12.7013 n: 29 approvals: 16 13 0 0
 CADB size: 2 utilities: 1.79268 1.75188 1.72177 1.69036 n: 3 approvals: 2 1 0 0
 CBAD size: 1 utilities: 0.893553 0.878045 0.859708 0.850641 n: 2 approvals: 1 1 0 0
 CBDA size: 13 utilities: 11.8136 11.5785 11.2078 10.5774 n: 29 approvals: 13 13 3 0
 CDAB size: 50 utilities: 47.0008 46.1416 41.2368 40.08 n: 101 approvals: 50 50 1 0
 CDBA size: 207 utilities: 199.339 192.531 170.413 160.748 n: 416 approvals: 207 205 4 0
 DCAB size: 126 utilities: 110.14 105.194 86.8122 82.1339 n: 252 approvals: 126 126 0 0
 DCBA size: 143 utilities: 136.57 132.478 107.115 102.498 n: 286 approvals: 143 143 0 0

Voter blocs (9):
 ABCD size: 222 utilities: 203.508 191.912 181.541 81.4973 n: 666 approvals: 222 222 222 0
 ACBD size: 3 utilities: 2.72246 2.66699 2.64001 1.44465 n: 9 approvals: 3 3 3 0
 BACD size: 105 utilities: 99.6423 96.6612 93.2713 45.6825 n: 315 approvals: 105 105 105 0
 BCAD size: 126 utilities: 115.448 111.886 107.805 60.3031 n: 378 approvals: 126 126 126 0
 CABD size: 2 utilities: 1.74751 1.71096 1.68104 1.03931 n: 6 approvals: 2 2 2 0
 CBAD size: 190 utilities: 173.021 165.225 156.2 107.792 n: 529 approvals: 190 185 154 0
 CBDA size: 12 utilities: 9.37905 8.60939 8.40187 8.26696 n: 13 approvals: 12 1 0 0
 CDBA size: 17 utilities: 12.8408 12.2676 11.7092 11.3002 n: 32 approvals: 17 15 0 0
 DCBA size: 323 utilities: 293.413 174.803 153.399 139.645 n: 350 approvals: 323 27 0 0
*/
