#!/usr/bin/env python3
"""Build fleet.db from the boat books in ~/Downloads.

Reads every .xlsm statement-of-account workbook and the ops-manual PDF
folder, writes a fresh SQLite database next to this script. Never writes
to the source files. Manual entries made in the app live in their own
table and survive re-ingest.
"""
import re
import sqlite3
import sys
from datetime import datetime
from pathlib import Path

from python_calamine import CalamineWorkbook

SHEETS_DIR = Path.home() / "Downloads" / "Boat Sheets most recent"
MANUALS_DIR = Path.home() / "Downloads" / "Current Boat Books"
DB_PATH = Path(__file__).parent / "fleet.db"
SPECS_CSV = Path(__file__).parent / "fleet_specs.csv"
OVERRIDES = Path(__file__).parent / "fleet_overrides.txt"
FLEET_URL = "https://conchcharters.com/en/our-fleet.html"

MONTH_RE = re.compile(r"^([A-Za-z]{3})-(\d{2})$")
NON_BOOK = {"conch charters turnaround sheet"}  # template files to skip


def norm(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name.lower())


def month_key(sheet_name: str):
    m = MONTH_RE.match(sheet_name.strip())
    if not m:
        return None
    try:
        dt = datetime.strptime(f"{m.group(1)}-{m.group(2)}", "%b-%y")
    except ValueError:
        return None
    return dt.strftime("%Y-%m")


def num(v):
    """Cached cell value -> float, treating errors/text as None."""
    if v is None:
        return None
    if isinstance(v, (int, float)):
        return float(v)
    s = str(v).strip()
    if not s or s.startswith("#"):
        return None
    try:
        return float(s.replace(",", "").replace("$", ""))
    except ValueError:
        return None


def parse_month_sheet(rows):
    """Return dict for one monthly statement sheet (list-of-lists), or None."""
    rows = rows[:400]

    def cell(r, c):
        if 0 <= r - 1 < len(rows) and 0 <= c - 1 < len(rows[r - 1]):
            v = rows[r - 1][c - 1]
            return None if v == "" else v
        return None

    def label(r):
        v = cell(r, 1)
        return str(v).strip().lower() if v is not None else ""

    anchors = {}
    for i in range(1, len(rows) + 1):
        a = label(i)
        if a.startswith("balance brought forward"):
            anchors["bbf"] = i
        elif a.startswith("expenses"):
            anchors["exp"] = i
        elif a.startswith("total expenses"):
            anchors["exp_total"] = i
            anchors["exp_total_col"] = 1
        elif a.startswith("income"):
            anchors["inc"] = i
        elif a.startswith("total income"):
            anchors["inc_total"] = i
        # older books put the totals label one column to the right
        b = cell(i, 2)
        bl = str(b).strip().lower() if b is not None else ""
        if "exp_total" not in anchors and bl.startswith("total expenses"):
            anchors["exp_total"] = i
            anchors["exp_total_col"] = 2
        c3 = cell(i, 3)
        if c3 is not None and str(c3).strip().lower().startswith("total due by"):
            anchors["due"] = i

    if "bbf" not in anchors:
        return None

    # Some busy months lost the "Total expenses" label cell: find the totals
    # row by shape instead (last row in the expenses block whose Total column
    # is filled; normal expense lines leave column D empty).
    if "exp_total" not in anchors:
        start = anchors.get("exp", anchors["bbf"]) + 1
        end = anchors.get("inc", len(rows) + 1)
        for i in range(end - 1, start - 1, -1):
            if num(cell(i, 4)) is not None and (
                    num(cell(i, 2)) is not None or num(cell(i, 3)) is not None):
                anchors["exp_total"] = i
                anchors["exp_total_col"] = 1
                break
    # Still nothing: some sheets have no totals row at all. Treat everything
    # up to the income section as expense lines (col 0 = sum lines ourselves),
    # and an entirely empty month as a zero-activity statement.
    if "exp_total" not in anchors:
        anchors["exp_total"] = anchors.get("inc", anchors["bbf"] + 1)
        anchors["exp_total_col"] = 0

    out = {
        "opening": num(cell(anchors["bbf"], 4)),
        "rates": {
            "turnaround": num(cell(2, 9)),
            "ta_tech": num(cell(2, 10)),
            "senior_mech": num(cell(2, 11)),
            "aesthetic": num(cell(2, 12)),
        },
        "expenses": [],
        "incomes": [],
    }

    r = anchors["exp_total"]
    if anchors["exp_total_col"] == 1:
        out["exp_credits"] = num(cell(r, 2)) or 0.0
        out["exp_debits"] = num(cell(r, 3)) or 0.0
    elif anchors["exp_total_col"] == 2:
        out["exp_credits"] = 0.0
        out["exp_debits"] = num(cell(r, 3)) or 0.0
    else:
        out["exp_credits"] = out["exp_debits"] = None  # filled from lines below

    # Books come in two labour layouts: three technician blocks (cols E-M),
    # or one block (cols E-G) followed by Parts/markup/Outside columns that
    # must NOT be read as labour. The three-block layout is identifiable by
    # a MECHANIC header above the expense lines.
    hdr_end = min(anchors.get("exp", anchors["bbf"]) + 2, len(rows))
    three_tech = any(
        cell(i, c) is not None and "mechanic" in str(cell(i, c)).lower()
        for i in range(1, hdr_end + 1) for c in range(5, 14))

    # Start right after the opening balance so owner payments entered above
    # the "Expenses:" header are captured too.
    for i in range(anchors["bbf"] + 1, anchors["exp_total"]):
        if i == anchors.get("exp"):
            continue
        desc = cell(i, 1)
        credit = num(cell(i, 2)) or 0.0
        debit = num(cell(i, 3)) or 0.0
        if three_tech:
            lab = [num(cell(i, c)) or 0.0 for c in (5, 6, 7, 8, 9, 10, 11, 12, 13)]
        else:
            lab = [num(cell(i, c)) or 0.0 for c in (5, 6, 7)] + [0.0] * 6
        # flat turnaround fees are double-recorded in the labour $ column in
        # many books; they are not technician labour
        if desc and str(desc).strip().lower().startswith("turnaround fee"):
            lab = [0.0] * 9
        if credit == 0 and debit == 0 and not any(lab):
            continue
        out["expenses"].append({
            "desc": str(desc).strip() if desc else "",
            "credit": credit, "debit": debit,
            "ta_h": lab[0], "ta_m": lab[1], "ta_usd": lab[2],
            "sm_h": lab[3], "sm_m": lab[4], "sm_usd": lab[5],
            "as_h": lab[6], "as_m": lab[7], "as_usd": lab[8],
        })

    if "inc" in anchors and "inc_total" in anchors:
        out["income_total"] = num(cell(anchors["inc_total"], 2)) or 0.0
        for i in range(anchors["inc"] + 1, anchors["inc_total"]):
            desc = cell(i, 1)
            amount = num(cell(i, 2))
            if amount is None:
                amount = num(cell(i, 3)) or 0.0  # some books put income in col C
            own_name = cell(3, 2)  # sheet header carries the boat's name
            toks = []
            for c in range(5, 14):
                v = cell(i, c)
                if v is None:
                    continue
                s = str(v).strip()
                # keep only descriptive text: drop blanks, bare numbers
                # (days/nights/dinghy columns) and the boat's own name
                if not s or num(v) is not None:
                    continue
                if own_name and norm(s) == norm(str(own_name)):
                    continue
                toks.append(s)
            details = " · ".join(toks)
            if amount == 0 and not (desc and str(desc).strip()) and not details:
                continue
            out["incomes"].append({
                "desc": str(desc).strip() if desc else "",
                "amount": amount, "details": details,
            })
    else:
        out["income_total"] = 0.0

    # labour totals from the lines themselves (totals rows vary across book vintages)
    out["ta_total"] = sum(e["ta_usd"] for e in out["expenses"])
    out["sm_total"] = sum(e["sm_usd"] for e in out["expenses"])
    out["as_total"] = sum(e["as_usd"] for e in out["expenses"])
    if out["exp_credits"] is None:
        out["exp_credits"] = sum(e["credit"] for e in out["expenses"])
        out["exp_debits"] = sum(e["debit"] for e in out["expenses"])
    out["closing"] = num(cell(anchors["due"], 4)) if "due" in anchors else None
    if out["closing"] is None and out["opening"] is not None:
        out["closing"] = (out["opening"] + out["exp_credits"]
                          - out["exp_debits"] + out["income_total"])
    return out


def fetch_active_fleet():
    """Boat names currently listed on conchcharters.com (normalized), or None
    when the site is unreachable — callers then keep the previous flags."""
    import urllib.request
    name_re = re.compile(r"\(&nbsp;\s*([^&<)]+?)\s*&nbsp;\)")
    names, seen_pages = set(), set()
    for page in range(1, 8):
        url = f"{FLEET_URL}?resultsPage={page}&resultsPerPage=50"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                html = r.read().decode("utf-8", "replace")
        except Exception as e:
            # all-or-nothing: a partial list would wrongly flag boats as departed
            print(f"fleet page {page}: {e}", file=sys.stderr)
            return None
        page_names = {m.strip() for m in name_re.findall(html)}
        if not page_names or frozenset(page_names) in seen_pages:
            break  # past the last page, or the site ignored the page param
        seen_pages.add(frozenset(page_names))
        names |= page_names
    return names or None


def previous_active(cur):
    """active flags from the last ingest, by normalized name."""
    try:
        return {norm(n): a for n, a in cur.execute("SELECT name, active FROM boats")}
    except sqlite3.OperationalError:
        return {}


def load_overrides():
    """fleet_overrides.txt: '- Name' forces left-fleet, '+ Name' forces active."""
    rules = {}
    if OVERRIDES.exists():
        for line in OVERRIDES.read_text().splitlines():
            line = line.strip()
            if line.startswith("-"):
                rules[norm(line[1:])] = 0
            elif line.startswith("+"):
                rules[norm(line[1:])] = 1
    return rules


def load_specs():
    """Specs copied from the old app's fleet master list, keyed by normalized name."""
    specs = {}
    if not SPECS_CSV.exists():
        return specs
    import csv
    with open(SPECS_CSV, newline="") as f:
        for row in csv.DictReader(f):
            specs[norm(row.get("Yacht Name", ""))] = row
    return specs


def match_manual(nname, manuals):
    for mkey, mpath in manuals.items():
        if mkey == nname or mkey in nname or nname in mkey:
            return mpath
    return None


def main():
    if not SHEETS_DIR.is_dir():
        print(f"ERROR: {SHEETS_DIR} not found", file=sys.stderr)
        return 1

    books = sorted(p for p in SHEETS_DIR.glob("*.xlsm")
                   if not any(t in p.stem.lower() for t in NON_BOOK))
    manuals = {}
    if MANUALS_DIR.is_dir():
        for p in MANUALS_DIR.glob("*.pdf"):
            key = norm(re.sub(r"(?i)operations\s*manual\s*-?", "", p.stem)
                       .replace("Conch-DC", ""))
            manuals[key] = str(p)
    specs = load_specs()

    con = sqlite3.connect(DB_PATH)
    cur = con.cursor()

    web_fleet = fetch_active_fleet()
    prev = previous_active(cur)
    overrides = load_overrides()
    if web_fleet is not None and len(web_fleet) < 20:
        # far fewer boats than the fleet has ever had: probably a broken or
        # partial page, not mass departures — don't trust it
        print(f"fleet list: only {len(web_fleet)} boats found, ignoring",
              file=sys.stderr)
        web_fleet = None
    if web_fleet is None:
        print("fleet list: site unreachable, keeping previous flags", file=sys.stderr)
    else:
        web_fleet = {norm(n) for n in web_fleet}
        print(f"fleet list: {len(web_fleet)} boats on conchcharters.com")

    def is_active(nname):
        if nname in overrides:
            return overrides[nname]
        if web_fleet is not None:
            return 1 if any(nname == w or w in nname or nname in w
                            for w in web_fleet) else 0
        return prev.get(nname, 1)

    cur.executescript("""
        CREATE TABLE IF NOT EXISTS manual_entries(
            id INTEGER PRIMARY KEY, boat_name TEXT, entry_date TEXT,
            description TEXT, credit REAL DEFAULT 0, debit REAL DEFAULT 0,
            tech_type TEXT, labour_hours REAL DEFAULT 0, labour_usd REAL DEFAULT 0,
            created_at TEXT);
        DROP TABLE IF EXISTS boats;
        DROP TABLE IF EXISTS statements;
        DROP TABLE IF EXISTS expense_lines;
        DROP TABLE IF EXISTS income_lines;
        DROP TABLE IF EXISTS meta;
        CREATE TABLE boats(
            id INTEGER PRIMARY KEY, name TEXT UNIQUE, book_file TEXT,
            manual_pdf TEXT, image TEXT, active INTEGER DEFAULT 1,
            make TEXT, model TEXT, year TEXT,
            length_ft TEXT, cabins TEXT, heads TEXT, berths TEXT, vessel_type TEXT,
            first_month TEXT, last_month TEXT, closing_balance REAL);
        CREATE TABLE statements(
            id INTEGER PRIMARY KEY, boat_id INTEGER, month TEXT,
            opening REAL, exp_credits REAL, exp_debits REAL,
            income_total REAL, closing REAL,
            ta_usd REAL, sm_usd REAL, as_usd REAL,
            rate_turnaround REAL, rate_ta REAL, rate_sm REAL, rate_as REAL);
        CREATE TABLE expense_lines(
            id INTEGER PRIMARY KEY, boat_id INTEGER, month TEXT,
            description TEXT, credit REAL, debit REAL,
            ta_h REAL, ta_m REAL, ta_usd REAL,
            sm_h REAL, sm_m REAL, sm_usd REAL,
            as_h REAL, as_m REAL, as_usd REAL);
        CREATE TABLE income_lines(
            id INTEGER PRIMARY KEY, boat_id INTEGER, month TEXT,
            description TEXT, amount REAL, details TEXT);
        CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);
    """)

    src_sig = f"{len(books)}:{int(max((p.stat().st_mtime for p in books), default=0))}"
    n_sheets = 0
    for path in books:
        name = path.stem
        nname = norm(name)
        try:
            wb = CalamineWorkbook.from_path(path)
        except Exception as e:
            print(f"SKIP {name}: {e}", file=sys.stderr)
            continue
        sp = specs.get(nname) or next(
            (v for k, v in specs.items() if k and (k in nname or nname in k)), {})
        img = Path(__file__).parent / "images" / f"{sp.get('Yacht ID', '')}.jpg"
        cur.execute(
            "INSERT INTO boats(name, book_file, manual_pdf, image, active,"
            " make, model, year, length_ft, cabins, heads, berths, vessel_type)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (name, str(path), match_manual(nname, manuals),
             str(img) if img.is_file() else None, is_active(nname),
             sp.get("Make"), sp.get("Model"), sp.get("Year"),
             sp.get("Length (ft)"), sp.get("Cabins"), sp.get("Heads"),
             sp.get("Berths"), sp.get("Vessel Type")))
        boat_id = cur.lastrowid

        months = []
        for sheet in wb.sheet_names:
            mk = month_key(sheet)
            if not mk:
                continue
            data = parse_month_sheet(wb.get_sheet_by_name(sheet).to_python())
            if data is None:
                print(f"  unparsed sheet {name}/{sheet}", file=sys.stderr)
                continue
            months.append(mk)
            n_sheets += 1
            cur.execute(
                "INSERT INTO statements(boat_id, month, opening, exp_credits,"
                " exp_debits, income_total, closing, ta_usd, sm_usd, as_usd,"
                " rate_turnaround, rate_ta, rate_sm, rate_as)"
                " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                (boat_id, mk, data["opening"], data["exp_credits"],
                 data["exp_debits"], data["income_total"], data["closing"],
                 data["ta_total"], data["sm_total"], data["as_total"],
                 data["rates"]["turnaround"], data["rates"]["ta_tech"],
                 data["rates"]["senior_mech"], data["rates"]["aesthetic"]))
            for e in data["expenses"]:
                cur.execute(
                    "INSERT INTO expense_lines(boat_id, month, description,"
                    " credit, debit, ta_h, ta_m, ta_usd, sm_h, sm_m, sm_usd,"
                    " as_h, as_m, as_usd) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    (boat_id, mk, e["desc"], e["credit"], e["debit"],
                     e["ta_h"], e["ta_m"], e["ta_usd"],
                     e["sm_h"], e["sm_m"], e["sm_usd"],
                     e["as_h"], e["as_m"], e["as_usd"]))
            for inc in data["incomes"]:
                cur.execute(
                    "INSERT INTO income_lines(boat_id, month, description,"
                    " amount, details) VALUES(?,?,?,?,?)",
                    (boat_id, mk, inc["desc"], inc["amount"], inc["details"]))

        if months:
            months.sort()
            cur.execute(
                "UPDATE boats SET first_month=?, last_month=?, closing_balance="
                "(SELECT closing FROM statements WHERE boat_id=? AND month=?)"
                " WHERE id=?",
                (months[0], months[-1], boat_id, months[-1], boat_id))
        print(f"  {name}: {len(months)} months")

    cur.execute("INSERT INTO meta VALUES('source_sig', ?)", (src_sig,))
    cur.execute("INSERT INTO meta VALUES('ingested_at', ?)",
                (datetime.now().isoformat(timespec="seconds"),))
    con.commit()
    con.close()
    print(f"Done: {len(books)} boats, {n_sheets} statements -> {DB_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
