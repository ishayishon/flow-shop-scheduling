# Flow-shop scheduling with job rejection

מימוש C++17 לשלוש יוריסטיקות לבחירת עבודות לדחייה תחת תקציב, ולפתרון בסיס המונה קבוצות דחייה ומשתמש ב־WSPT-MCI לתזמון העבודות שנותרו.

## קבצים

- `exactRejectionFlowshop.cpp` — H1, H2, H3, פתרון הבסיס ויצירת הניסויים.
- `compare_runs.cpp` — שילוב תוצאות ההרצות ויצירת טבלאות וגרפים.
- `runs/run_001` עד `run_010` — הנתונים ששימשו לתוצאות המסכמות בדוח.
- `runs/comparison` — טבלאות השוואה וגרפים בין ההרצות.

## קימפול

נדרש מהדר התומך ב־C++17.

```powershell
g++ -std=c++17 -O2 -Wall -Wextra -pedantic exactRejectionFlowshop.cpp -o exactRejectionFlowshop.exe
g++ -std=c++17 -O2 -Wall -Wextra -pedantic compare_runs.cpp -o compare_runs.exe
```

## עיון בתוצאות הקיימות

הנתונים הדרושים לקריאת התוצאות כבר נמצאים בתיקייה. אין צורך להריץ את הניסוי מחדש כדי לעיין בהם.

ליצירת סיכום מתוך תיקיות ההרצות הקיימות:

```powershell
.\compare_runs.exe compare runs
```

הפעולה כותבת מחדש את פלטי ההשוואה והגרפים תחת `runs/comparison`.

## יצירת ניסוי חדש

```powershell
.\exactRejectionFlowshop.exe
```

ברירת המחדל בקוד מריצה 5,000 מקרים קטנים ו־150 מקרים גדולים, עד 200 עבודות, ולכן ההרצה עשויה להיות ממושכת. היא יוצרת תיקיית `run_XXX` חדשה וכותבת מחדש גם את קובצי ה־CSV בשורש תיקיית הקוד. הוספת הרצות תשנה את הסיכום הבא של כל תיקיות ההרצה.

הזרע האקראי נבחר בזמן ההרצה כאשר `randomSeed = 0`, ולכן הרצה חדשה לא צפויה לייצר את אותם מספרים. קלטי המקרים הקטנים נשמרו ב־`benchmark_trial_jobs.csv`. במקרים הגדולים נשמרו תוצאות ומדידות זמן, אך לא נמצאו קלטי העבודות המלאים לשחזורם.

## Report

The final academic report is available in [docs/Final_Project1.pdf](docs/Final_Project1.pdf). The ten saved runs contain 50,000 quality benchmark instances and 1,500 scalability instances. The aggregate metrics in Tables 1–6 match the saved instance-level results after rounding.
