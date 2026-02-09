# C++ Backtesting Engine

A backtesting engine for trading algorithms with **OHLC** (Open, High, Low, Close) bar data.

## Architecture

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│  OHLC Data      │────▶│  Backtester      │────▶│  Reports        │
│  (CSV file)     │     │  - Data feed     │     │  - Metrics      │
└─────────────────┘     │  - Strategy      │     │  - Trade log    │
                        │  - Simulator     │     │  - Equity curve │
                        └────────┬─────────┘     └─────────────────┘
                                 │
                        ┌────────▼─────────┐
                        │  Trading Algo    │
                        │  (Strategy .cpp  │
                        │   or config)     │
                        └──────────────────┘
```

### How backtesting works (no look-ahead)

1. **Data feed**: Bars are fed in chronological order. The strategy only sees the current bar and all previous bars—never future data.
2. **On each bar**: The engine calls your strategy’s `onBar(bar, context)`. You can place orders using the context.
3. **Execution**: Orders are simulated at the **next bar’s open** (or current close, depending on mode), so you don’t get “perfect” fills at the bar that generated the signal—this avoids look-ahead bias.
4. **Simulator**: Tracks positions, cash, commissions, and equity. No fractional fills in the basic version.

### Key components

| Component   | Role |
|------------|------|
| **Bar**    | Single OHLC bar (timestamp, O, H, L, C, optional volume). |
| **DataSource** | Loads OHLC from CSV and iterates bars in order. |
| **IStrategy**  | Your algo: implement `onBar()`, use context to place orders. |
| **Simulator**  | Executes orders, keeps positions and P&amp;L. |
| **Backtester** | Runs the loop: bar → strategy → orders → simulator → next bar. |
| **Report**     | Computes metrics (return, Sharpe, max drawdown, win rate) and writes reports. |

### Strategy as a file

- **C++ strategy**: Implement the `IStrategy` interface in a `.cpp` file (e.g. under `strategies/`). The engine is built with your strategy linked in; you choose which strategy runs in `main` or via a CLI flag.
- **Config-based strategy** (optional): A simple strategy (e.g. MA crossover) can be driven by a JSON/config file so you can change parameters without recompiling.

## Build

Requires a **C++17** compiler and [CMake](https://cmake.org/) (optional).

**With CMake (recommended):**
```bash
mkdir build && cd build
cmake ..
cmake --build .
# Run from build dir so default data path works:
./backtester
```

**With build.bat (Windows, from `build/`):**
```batch
build.bat
```
Produces `backtester.exe` and `test_runner.exe`.

## Testing

A small test suite lives in `tests/test_runner.cpp` (no external test framework). It checks:

- **Simulator**: Long trade PnL, commission handling.
- **DataSource**: CSV load, 15m bar aggregation.

Run tests after building:
```bash
./test_runner    # or test_runner.exe on Windows
```

## Strategies

| Strategy        | Description |
|----------------|-------------|
| **sma_crossover** | Moving-average crossover (default fast=9, slow=21). Use `--fast`, `--slow`, `--size` (position size as fraction of equity). |
| **ctm**        | CTM-style trend (long/short SMAs). Optional Kalman trend filter: `--ctm-kalman`, `--ctm-kalman-long`, `--ctm-kalman-short`. |
| **orb**        | Opening range breakout: first bar at session = range, next bar = trigger; stop at ORB high/low. 15% equity per day, EOD exit. Session time in UTC: `--orb-session-hour`, `--orb-session-minute`. Position size: `--size 0.2` for 20%. |

## Run

**Single-symbol (CSV):**
```bash
./backtester --data data/sample_ohlc.csv --strategy sma_crossover
./backtester --data data/sample_ohlc.csv --strategy orb --bar 15m
./backtester --data data/sample_ohlc.csv --cash 100000 --commission 10
```

**Single-symbol (Databento folder):**
```bash
./backtester --databento-dir path/to/glbx --symbol NQU5 --strategy ctm --bar 15m --ctm-kalman
```

**All symbols in a Databento dir:** omit `--symbol` to run one account per symbol and print a combined table.

Reports are written to `reports/` (trades.csv, equity_curve.csv, report.txt). Default dir: `reports`; override with `--reports-dir`.

### CLI options

| Option | Description |
|--------|-------------|
| `--data <path>` | CSV file path (default: data/sample_ohlc.csv). |
| `--strategy <name>` | Strategy: `sma_crossover`, `ctm`, `orb`. |
| `--databento-dir <dir>` | Load OHLC from Databento-style filenames in this directory. |
| `--symbol <sym>` | Filter to one symbol when using `--databento-dir`. Empty = run all symbols. |
| `--bar <res>` | Bar resolution: `1m`, `15m`, `1h` (aggregate from 1m). Shortcuts: `-15m`, `-1h`. |
| `--cash <n>` | Initial cash. |
| `--commission <n>` | Commission per trade. |
| `--slippage <fraction>` | Slippage as fraction of fill price (e.g. 0.001 = 0.1%). Longs fill at open×(1+slippage), shorts at open×(1−slippage). |
| `--reports-dir <dir>` | Output directory for reports. |
| `--fast`, `--slow` | SMA periods (sma_crossover / ctm). |
| `--size <0..1>` | Position size as fraction of equity (e.g. 0.15 = 15%). ORB default 15% if not set. |
| `--ctm-kalman`, `--ctm-kalman-long`, `--ctm-kalman-short` | Enable Kalman trend filter for CTM. |
| `--orb-session-hour`, `--orb-session-minute` | Session start in UTC (e.g. 14:30 for 9:30 ET). |

## Input: OHLC format

CSV with columns (order can vary; header is required):

- `timestamp` or `date` (ISO or YYYY-MM-DD)
- `open`, `high`, `low`, `close` (numeric)
- `volume` (optional)

Example:

```csv
timestamp,open,high,low,close,volume
2024-01-02,100.0,101.5,99.0,100.5,1000000
2024-01-03,100.5,102.0,100.0,101.0,1200000
```

## Reports

After the backtest, the engine produces:

- **Console**: Summary (total return %, max drawdown %, number of trades, win rate).
- **Files** (in `reports/`): Trade log (CSV), equity curve (CSV), and a text/JSON summary of metrics.

## Adding your own strategy

1. Create a new file under `strategies/` (e.g. `my_strategy.cpp`).
2. Implement `IStrategy`: constructor/destructor and `onBar(const Bar& bar, IContext& ctx)`.
3. In `onBar`, use `ctx.placeOrder(...)` to go long/short or close.
4. Register your strategy in `main.cpp` (or via a factory) and pass its name on the command line.

See `strategies/example_sma_strategy.cpp` for a minimal example.
