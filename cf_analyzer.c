#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wininet.h>
#include <time.h>
#include "cJSON.h"

#pragma comment(lib, "wininet.lib")
#define BUFFER_SIZE 4096

typedef struct {
    char contestName[128];
    int rank;
    int oldRating;
    int newRating;
    time_t time;
    char division[10];
    int contestId;
} ContestRecord;

typedef struct {
    int rating;
    time_t solveTime;
    int contestId;
    int isContestSolve; // 1=比赛中通过，0=赛后补题
} ProblemRecord;

ContestRecord* contests = NULL;
int contestCount = 0;
ProblemRecord* problems = NULL;
int problemCount = 0;

typedef struct {
    char handle[64];
    int rating;
    int maxRating;
    char rank[32];
    char avatar[128];
    int contestCount;
    int last180ContestCount;
    int last180MaxRating;
} UserInfo;

UserInfo userInfo;

char* http_get(const char* url) {
    HINTERNET hInternet = InternetOpenA("CF-Analyzer/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return NULL;

    HINTERNET hConnect = InternetOpenUrlA(hInternet, url, NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return NULL;
    }

    char* response = NULL;
    DWORD totalRead = 0;
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;

    while (InternetReadFile(hConnect, buffer, BUFFER_SIZE - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        response = realloc(response, totalRead + bytesRead + 1);
        memcpy(response + totalRead, buffer, bytesRead);
        totalRead += bytesRead;
        response[totalRead] = '\0';
    }

    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return response;
}

void parse_user_info(const char* handle) {
    char url[256];
    sprintf(url, "https://codeforces.com/api/user.info?handles=%s", handle);
    char* json_str = http_get(url);
    if (!json_str) return;

    cJSON* root = cJSON_Parse(json_str);
    if (!root) { free(json_str); return; }

    cJSON* status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!status || strcmp(status->valuestring, "OK") != 0) {
        cJSON_Delete(root); free(json_str); return;
    }

    cJSON* result = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "result"), 0);
    if (!result) { cJSON_Delete(root); free(json_str); return; }

    strcpy(userInfo.handle, handle);
    cJSON* rating = cJSON_GetObjectItemCaseSensitive(result, "rating");
    cJSON* maxRating = cJSON_GetObjectItemCaseSensitive(result, "maxRating");
    cJSON* rank = cJSON_GetObjectItemCaseSensitive(result, "rank");
    cJSON* avatar = cJSON_GetObjectItemCaseSensitive(result, "avatar");

    if (rating) userInfo.rating = rating->valuedouble;
    if (maxRating) userInfo.maxRating = maxRating->valuedouble;
    if (rank) strcpy(userInfo.rank, rank->valuestring);
    if (avatar) strcpy(userInfo.avatar, avatar->valuestring);

    cJSON_Delete(root);
    free(json_str);
}

void parse_user_rating(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!status || strcmp(status->valuestring, "OK") != 0) {
        cJSON_Delete(root);
        return;
    }

    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (!cJSON_IsArray(result)) {
        cJSON_Delete(root);
        return;
    }

    contestCount = cJSON_GetArraySize(result);
    contests = (ContestRecord*)malloc(contestCount * sizeof(ContestRecord));
    userInfo.contestCount = contestCount;

    time_t one_year_ago = time(NULL) - 180 * 24 * 3600;
    userInfo.last180ContestCount = 0;
    userInfo.last180MaxRating = 0;

    for (int i = 0; i < contestCount; i++) {
        cJSON *item = cJSON_GetArrayItem(result, i);
        cJSON *contestName = cJSON_GetObjectItemCaseSensitive(item, "contestName");
        cJSON *rank = cJSON_GetObjectItemCaseSensitive(item, "rank");
        cJSON *oldRating = cJSON_GetObjectItemCaseSensitive(item, "oldRating");
        cJSON *newRating = cJSON_GetObjectItemCaseSensitive(item, "newRating");
        cJSON *timeSeconds = cJSON_GetObjectItemCaseSensitive(item, "ratingUpdateTimeSeconds");
        cJSON *contestId = cJSON_GetObjectItemCaseSensitive(item, "contestId");

        if (contestName && rank && oldRating && newRating && timeSeconds && contestId) {
            strncpy(contests[i].contestName, contestName->valuestring, sizeof(contests[i].contestName) - 1);
            contests[i].contestName[sizeof(contests[i].contestName) - 1] = '\0';
            contests[i].rank = rank->valuedouble;
            contests[i].oldRating = oldRating->valuedouble;
            contests[i].newRating = newRating->valuedouble;
            contests[i].time = (time_t)timeSeconds->valuedouble;
            contests[i].contestId = contestId->valuedouble;

            if (strstr(contests[i].contestName, "Div. 1 + Div. 2") || strstr(contests[i].contestName, "Div.1+Div.2")) {
                strcpy(contests[i].division, "Div.1+2");
            } else if (strstr(contests[i].contestName, "Div. 1")) {
                strcpy(contests[i].division, "Div.1");
            } else if (strstr(contests[i].contestName, "Div. 2")) {
                strcpy(contests[i].division, "Div.2");
            } else if (strstr(contests[i].contestName, "Div. 3")) {
                strcpy(contests[i].division, "Div.3");
            } else if (strstr(contests[i].contestName, "Div. 4")) {
                strcpy(contests[i].division, "Div.4");
            } else {
                strcpy(contests[i].division, "Other");
            }

            if (contests[i].time >= one_year_ago) {
                userInfo.last180ContestCount++;
                if (contests[i].newRating > userInfo.last180MaxRating) {
                    userInfo.last180MaxRating = contests[i].newRating;
                }
            }
        }
    }

    cJSON_Delete(root);
}

void parse_user_status(const char *json_str, int contestCount, ContestRecord* contests) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!status || strcmp(status->valuestring, "OK") != 0) {
        cJSON_Delete(root);
        return;
    }

    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (!cJSON_IsArray(result)) {
        cJSON_Delete(root);
        return;
    }

    int size = cJSON_GetArraySize(result);
    problems = (ProblemRecord*)malloc(size * sizeof(ProblemRecord));
    problemCount = 0;

    for (int i = 0; i < size; i++) {
        cJSON *submission = cJSON_GetArrayItem(result, i);
        cJSON *verdict = cJSON_GetObjectItemCaseSensitive(submission, "verdict");
        cJSON *problem = cJSON_GetObjectItemCaseSensitive(submission, "problem");
        cJSON *creationTime = cJSON_GetObjectItemCaseSensitive(submission, "creationTimeSeconds");
        cJSON *contestIdJson = cJSON_GetObjectItemCaseSensitive(submission, "contestId");

        if (!verdict || !problem || !creationTime || !contestIdJson) continue;
        if (strcmp(verdict->valuestring, "OK") != 0) continue;

        cJSON *rating = cJSON_GetObjectItemCaseSensitive(problem, "rating");
        if (!rating || !cJSON_IsNumber(rating)) continue;

        int contestId = contestIdJson->valuedouble;
        time_t solveTime = (time_t)creationTime->valuedouble;
        int isContestSolve = 0;

        for (int j = 0; j < contestCount; j++) {
            if (contests[j].contestId == contestId) {
                if (solveTime >= contests[j].time && solveTime <= contests[j].time + 2*3600) {
                    isContestSolve = 1;
                }
                break;
            }
        }

        problems[problemCount].rating = (int)rating->valuedouble;
        problems[problemCount].solveTime = solveTime;
        problems[problemCount].contestId = contestId;
        problems[problemCount].isContestSolve = isContestSolve;
        problemCount++;
    }

    cJSON_Delete(root);
}

const char* get_color_by_rating(int rating) {
    if (rating >= 3000) return "#FF0000";
    else if (rating >= 2400) return "#FF0000";
    else if (rating >= 2100) return "#AA00AA";
    else if (rating >= 1900) return "#0000FF";
    else if (rating >= 1600) return "#0000FF";
    else if (rating >= 1400) return "#00AA00";
    else if (rating >= 1200) return "#00AA00";
    else if (rating >= 1000) return "#0000FF";
    else return "#808080";
}

void print_contest_details() {
    printf("\n===== All Contest Details (Newest First) =====\n");
    for (int i = contestCount - 1; i >= 0; i--) {
        printf("%d. %s\n", contestCount - i, contests[i].contestName);
        printf("   Division: %s\n", contests[i].division);
        printf("   Rank: %d\n", contests[i].rank);
        printf("   Rating: %d -> %d\n", contests[i].oldRating, contests[i].newRating);
        printf("   Date: %s", ctime(&contests[i].time));

        int contestSolve = 0, upsolve = 0;
        for (int j = 0; j < problemCount; j++) {
            if (problems[j].contestId == contests[i].contestId) {
                if (problems[j].isContestSolve) contestSolve++;
                else upsolve++;
            }
        }
        printf("   Problems solved in contest: %d\n", contestSolve);
        printf("   Problems upsolved after contest: %d\n", upsolve);
        printf("----------------------------------------\n");
    }
}

void generate_html() {
    FILE *html = fopen("chart.html", "w");
    if (!html) return;

    #define BIN_COUNT 8
    const int bin_starts[BIN_COUNT] = {800, 1000, 1200, 1400, 1600, 1800, 2000, 2200};
    int bin_counts[BIN_COUNT] = {0};
    int contestSolveCount = 0, upsolveCount = 0;

    for (int i = 0; i < problemCount; i++) {
        int r = problems[i].rating;
        for (int j = 0; j < BIN_COUNT; j++) {
            if (r >= bin_starts[j] && r < bin_starts[j] + 200) {
                bin_counts[j]++;
                break;
            }
        }
        if (problems[i].isContestSolve) contestSolveCount++;
        else upsolveCount++;
    }

    fprintf(html, "<!DOCTYPE html>\n<html>\n<head>\n");
    fprintf(html, "<meta charset=\"utf-8\">\n");
    fprintf(html, "<title>Codeforces Analysis</title>\n");
    fprintf(html, "<script src=\"https://cdn.jsdelivr.net/npm/echarts/dist/echarts.min.js\"></script>\n");
    fprintf(html, "</head>\n<body>\n");

    fprintf(html, "<div style=\"padding: 20px; border-bottom: 1px solid #eee;\">\n");
    fprintf(html, "<h2 style=\"color: %s;\">User: %s (%s)</h2>\n", get_color_by_rating(userInfo.rating), userInfo.handle, userInfo.rank);
    fprintf(html, "<p>Current Rating: %d | Max Rating: %d</p>\n", userInfo.rating, userInfo.maxRating);
    fprintf(html, "<p>Contests Participated: %d | Last 180 Days: %d</p>\n", userInfo.contestCount, userInfo.last180ContestCount);
    fprintf(html, "<p>Last 180 Days Max Rating: %d</p>\n", userInfo.last180MaxRating);
    fprintf(html, "</div>\n");

    fprintf(html, "<div id=\"ratingChart\" style=\"width: 100%%; height: 400px; margin-top: 20px;\"></div>\n");
    fprintf(html, "<div id=\"problemChart\" style=\"width: 100%%; height: 400px; margin-top: 20px;\"></div>\n");
    fprintf(html, "<div id=\"upsolveChart\" style=\"width: 100%%; height: 300px; margin-top: 20px;\"></div>\n");

    fprintf(html, "<script>\n");
    fprintf(html, "var ratingData = [];\n");
    fprintf(html, "var contestNames = [];\n");
    fprintf(html, "var lineColor = '%s';\n", get_color_by_rating(userInfo.rating));
    for (int i = 0; i < contestCount; i++) {
        fprintf(html, "ratingData.push(%d);\n", contests[i].newRating);
        fprintf(html, "contestNames.push(\"%s\");\n", contests[i].contestName);
    }

    fprintf(html, "var problemLabels = [");
    for (int i = 0; i < BIN_COUNT; i++) {
        fprintf(html, "\"%d-%d\"%s", bin_starts[i], bin_starts[i] + 199, i == BIN_COUNT-1 ? "" : ",");
    }
    fprintf(html, "];\n");

    fprintf(html, "var problemData = [");
    for (int i = 0; i < BIN_COUNT; i++) {
        fprintf(html, "%d%s", bin_counts[i], i == BIN_COUNT-1 ? "" : ",");
    }
    fprintf(html, "];\n");

    fprintf(html, "var solveLabels = ['Contest Solved', 'Upsolved After Contest'];\n");
    fprintf(html, "var solveData = [%d, %d];\n", contestSolveCount, upsolveCount);

    fprintf(html, "var ratingChart = echarts.init(document.getElementById('ratingChart'));\n");
    fprintf(html, "ratingChart.setOption({\n");
    fprintf(html, "    title: { text: 'Rating Change' },\n");
    fprintf(html, "    xAxis: { type: 'category', data: contestNames, axisLabel: { rotate: 30 } },\n");
    fprintf(html, "    yAxis: { type: 'value', name: 'Rating' },\n");
    fprintf(html, "    series: [{ type: 'line', data: ratingData, itemStyle: { color: lineColor }, lineStyle: { color: lineColor } }]\n");
    fprintf(html, "});\n");

    fprintf(html, "var problemChart = echarts.init(document.getElementById('problemChart'));\n");
    fprintf(html, "problemChart.setOption({\n");
    fprintf(html, "    title: { text: 'Problem Difficulty Distribution' },\n");
    fprintf(html, "    xAxis: { type: 'category', data: problemLabels, axisLabel: { rotate: 30 } },\n");
    fprintf(html, "    yAxis: { type: 'value', name: 'Count' },\n");
    fprintf(html, "    series: [{ type: 'bar', data: problemData, itemStyle: { color: '#409EFF' } }]\n");
    fprintf(html, "});\n");

    fprintf(html, "var upsolveChart = echarts.init(document.getElementById('upsolveChart'));\n");
    fprintf(html, "upsolveChart.setOption({\n");
    fprintf(html, "    title: { text: 'Contest vs. Upsolved Problems' },\n");
    fprintf(html, "    tooltip: { trigger: 'item' },\n");
    fprintf(html, "    series: [{\n");
    fprintf(html, "        type: 'pie',\n");
    fprintf(html, "        data: [{ value: solveData[0], name: solveLabels[0], itemStyle: { color: '#409EFF' } },\n");
    fprintf(html, "               { value: solveData[1], name: solveLabels[1], itemStyle: { color: '#67C23A' } }]\n");
    fprintf(html, "    }]\n");
    fprintf(html, "});\n");

    fprintf(html, "</script>\n</body>\n</html>");
    fclose(html);
}

int main() {
    char handle[64];
    printf("Enter Codeforces username: ");
    scanf("%s", handle);

    parse_user_info(handle);

    char rating_url[256];
    sprintf(rating_url, "https://codeforces.com/api/user.rating?handle=%s", handle);
    char *rating_json = http_get(rating_url);
    if (rating_json) {
        parse_user_rating(rating_json);
        free(rating_json);
    }

    char status_url[256];
    sprintf(status_url, "https://codeforces.com/api/user.status?handle=%s&from=1&count=10000", handle);
    char *status_json = http_get(status_url);
    if (status_json) {
        parse_user_status(status_json, contestCount, contests);
        free(status_json);
    }

    printf("\n===== User Info =====\n");
    printf("Handle: %s\n", userInfo.handle);
    printf("Rank: %s\n", userInfo.rank);
    printf("Current Rating: %d\n", userInfo.rating);
    printf("Max Rating: %d\n", userInfo.maxRating);
    printf("Total Contests: %d\n", userInfo.contestCount);
    printf("Last 180 Days Contests: %d\n", userInfo.last180ContestCount);
    printf("Last 180 Days Max Rating: %d\n", userInfo.last180MaxRating);

    print_contest_details();

    generate_html();

    printf("\nDone! Open chart.html in browser.\n");

    if (contests) free(contests);
    if (problems) free(problems);

    // 关键：让程序暂停，窗口不关闭
    system("pause");
    return 0;
}