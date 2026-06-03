#!/usr/bin/env python3
"""Generate MVCC architecture diagrams with Chinese labels for Zhihu article."""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.font_manager as fm
from matplotlib.patches import FancyBboxPatch
import os

# Register CJK font
import sys
font_path = os.path.join(os.path.dirname(__file__), "..", "..", "..", "tmp",
                         "NotoSansCJKsc-Regular.otf")
if not os.path.exists(font_path):
    font_path = "/tmp/NotoSansCJKsc-Regular.otf"
if os.path.exists(font_path):
    fm.fontManager.addfont(font_path)
    fm.FontProperties(fname=font_path)
    # Find the Noto Sans CJK SC font
    for f in fm.fontManager.ttflist:
        if 'Noto Sans CJK SC' in f.name:
            plt.rcParams['font.family'] = f.name
            break
    else:
        plt.rcParams['font.family'] = 'sans-serif'
plt.rcParams['axes.unicode_minus'] = False

OUT = os.path.join(os.path.dirname(__file__), "..", "images")
os.makedirs(OUT, exist_ok=True)

C = {
    'mysql':   '#00758F',
    'pg':      '#336791',
    'rocks':   '#FF5722',
    'minidb':  '#2E7D32',
    'red':     '#E53935',
    'green':   '#43A047',
    'grey':    '#9E9E9E',
    'page':    '#FFF3E0',
    'undo':    '#E3F2FD',
    'readview':'#F3E5F5',
    'bg':      '#FAFAFA',
}

def bx(ax, x, y, w, h, text, face='white', size=9, weight='normal',
       tcolor='black', ec='#333', ew=1.5):
    r = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.12",
                       facecolor=face, edgecolor=ec, linewidth=ew)
    ax.add_patch(r)
    ax.text(x + w/2, y + h/2, text, ha='center', va='center',
            fontsize=size, fontweight=weight, color=tcolor)

def ar(ax, x1, y1, x2, y2, color='#333', lw=1.5):
    ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                arrowprops=dict(arrowstyle='->', color=color, lw=lw))

def save(fig):
    path = os.path.join(OUT, f"{fig._label}.png")
    fig.savefig(path, dpi=150, bbox_inches='tight', facecolor=C['bg'])
    plt.close(fig)
    print(f"  => {path}")

# ====== 图1: MySQL InnoDB MVCC ======
def d1_mysql():
    fig = plt.figure(figsize=(13, 6.2), facecolor=C['bg'])
    fig._label = "mysql_mvcc_flow"
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 13); ax.set_ylim(0, 6.2); ax.axis('off')

    ax.text(6.5, 5.85, "MySQL InnoDB MVCC: ReadView + Undo Log 版本链",
            ha='center', fontsize=14, fontweight='bold')

    ax.text(0.3, 5.2, "活跃事务表", fontsize=10, fontweight='bold', color=C['red'])
    trx = [("100", "COMMITTED"), ("101", "RUNNING"), ("102", "RUNNING"),
           ("103", "COMMITTED"), ("104", "COMMITTED"), ("105", "RUNNING")]
    for i, (tid, st) in enumerate(trx):
        y = 5.0 - i * 0.3
        fc = C['green'] if st == 'COMMITTED' else C['red']
        bx(ax, 0.3, y, 2.0, 0.24, f"trx {tid}: {st}",
           face=fc, size=7, tcolor='white', ec='none')

    bx(ax, 9.2, 3.8, 3.5, 2.0, "", face=C['readview'], ec='#7B1FA2')
    ax.text(10.95, 5.45, "ReadView (快照)", ha='center', fontsize=11,
            fontweight='bold', color='#7B1FA2')
    ax.text(9.5, 5.0, "m_ids = {101, 102, 105}\nmin_trx_id = 101\nmax_trx_id = 107",
            fontsize=8, fontfamily='monospace', va='top')

    bx(ax, 3.0, 1.2, 5.8, 3.5, "", face=C['page'], ec='#E65100', ew=2)
    ax.text(5.9, 4.55, "数据页", fontsize=10, fontweight='bold', color='#E65100')

    bx(ax, 3.5, 3.8, 4.8, 0.55,
       "trx_id=105  begin=105  end=MAX  roll_ptr\u2192  data: 'Carol'",
       face='white', size=7.5, ec=C['red'], ew=1.8)
    ax.text(3.7, 4.42, "v3 (当前版本，未提交)", fontsize=8, color=C['red'],
            fontweight='bold')

    bx(ax, 3.7, 2.9, 4.8, 0.55,
       "trx_id=104  begin=104  end=105  roll_ptr\u2192  data: 'Bob'",
       face=C['undo'], size=7.5, ec='#1976D2', ew=1.5)
    ax.text(3.9, 3.5, "v2 (Undo Log)", fontsize=8, color='#1976D2', fontweight='bold')

    bx(ax, 3.9, 2.0, 4.8, 0.55,
       "trx_id=100  begin=100  end=104  roll_ptr=null  data: 'Alice'",
       face=C['undo'], size=7.5, ec='#1976D2', ew=1.5)
    ax.text(4.1, 2.6, "v1 (Undo Log)", fontsize=8, color='#1976D2', fontweight='bold')

    ar(ax, 6.2, 3.8, 6.2, 3.5, color='#666', lw=1.2)
    ar(ax, 6.5, 2.9, 6.5, 2.6, color='#666', lw=1.2)

    ar(ax, 9.2, 4.2, 8.4, 4.1, color='#7B1FA2', lw=2)
    ax.text(8.8, 4.35, "可见性判断", fontsize=7, color='#7B1FA2', ha='center')

    ax.text(8.6, 3.8, "X  trx_id=105", fontsize=7.5, color=C['red'])
    ax.text(8.6, 2.9, "V  trx_id=104", fontsize=7.5, color=C['green'])
    ax.text(8.6, 2.0, "V  trx_id=100", fontsize=7.5, color=C['green'])

    ax.text(7.5, 1.35, "沿 roll_ptr 回溯到\nReadView 创建时有可见的版本",
            fontsize=8, ha='center', color='#333')
    ar(ax, 7.5, 1.6, 7.5, 2.0, color='#333', lw=1)

    bx(ax, 0.3, 0.3, 2.5, 0.7,
       "   = 已提交的行\n   = Undo Log 版本链\n   = 不可见版本",
       face='white', size=7, ec='#CCC')
    ax.add_patch(plt.Rectangle((0.5, 0.75), 0.15, 0.12, facecolor=C['green'], ec='#999'))
    ax.text(0.73, 0.81, "已提交", fontsize=6.5, va='center')
    ax.add_patch(plt.Rectangle((0.5, 0.53), 0.15, 0.12, facecolor=C['undo'], ec='#999'))
    ax.text(0.73, 0.59, "版本链", fontsize=6.5, va='center')
    ax.add_patch(plt.Rectangle((0.5, 0.31), 0.15, 0.12, facecolor=C['red'], ec='#999'))
    ax.text(0.73, 0.37, "不可见", fontsize=6.5, va='center')

    save(fig)

# ====== 图2: IsVisible 决策流程图 ======
def d2_vis():
    fig = plt.figure(figsize=(10, 7), facecolor=C['bg'])
    fig._label = "visibility_flow"
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 10); ax.set_ylim(0, 7); ax.axis('off')

    ax.text(5, 6.7, "MiniDB IsVisible() 可见性判断流程",
            ha='center', fontsize=14, fontweight='bold')

    bx(ax, 3.5, 6.0, 3, 0.4, "开始: 检查 TupleMeta", face='#ECEFF1', size=9, ec='#666')

    ar(ax, 5, 6.0, 5, 5.65, color='#333')
    bx(ax, 3.5, 5.3, 3, 0.4, "begin_ts=0 且 我的事务?",
       face=C['readview'], size=8.5, ec='#7B1FA2')

    ar(ax, 5, 5.3, 5, 4.95, color=C['green'], lw=1.5)
    ax.text(5.15, 5.1, '是', fontsize=7, color=C['green'])
    bx(ax, 3.5, 4.7, 3, 0.3, "end_ts = TS_MAX ?", face='#C8E6C9', size=8, ec=C['green'])

    ar(ax, 5, 4.7, 5, 4.4, color=C['green'], lw=1.5)
    ax.text(5.15, 4.5, '是', fontsize=7, color=C['green'])
    bx(ax, 3.8, 4.1, 2.4, 0.35, "YES: VISIBLE\n(读自己的写入)", face='#A5D6A7',
       size=8, weight='bold', ec='#1B5E20')

    ar(ax, 3.5, 4.85, 1.5, 4.85, color=C['red'], lw=1.5)
    ax.text(2.0, 4.95, '否\n(自己删了)', fontsize=6, color=C['red'], ha='center')
    bx(ax, 0.3, 4.7, 1.8, 0.3, "NO: INVISIBLE", face='#EF9A9A',
       size=7.5, ec='#B71C1C')

    ar(ax, 6.5, 5.5, 7.5, 5.5, color=C['red'], lw=1.5)
    ax.text(7.0, 5.6, '否', fontsize=7, color=C['red'])
    bx(ax, 7.2, 5.3, 2.5, 0.4, "begin_ts = 0 ?\n(别人的脏数据)", face='#FFCDD2',
       size=8, ec=C['red'])

    ar(ax, 8.45, 5.3, 8.45, 4.95, color=C['red'])
    ax.text(8.6, 5.1, '是', fontsize=7, color=C['red'])
    bx(ax, 7.2, 4.8, 2.5, 0.3, "NO: INVISIBLE (脏数据)", face='#EF9A9A',
       size=7.5, ec='#B71C1C')

    ar(ax, 8.45, 4.8, 8.45, 4.5, color='#333')
    bx(ax, 7.2, 4.2, 2.5, 0.35, "begin_ts > read_ts ?", face='#FFCDD2',
       size=8, ec=C['red'])
    ar(ax, 8.45, 4.2, 8.45, 3.85, color=C['red'])
    ax.text(8.6, 4.0, '是', fontsize=7, color=C['red'])
    bx(ax, 7.2, 3.7, 2.5, 0.3, "NO: INVISIBLE\n(快照之后提交)", face='#EF9A9A',
       size=7.5, ec='#B71C1C')

    ar(ax, 8.45, 3.7, 8.45, 3.4, color='#333')
    bx(ax, 7.2, 3.1, 2.5, 0.35, "end_ts != MAX\n且 <= read_ts ?", face='#FFCDD2',
       size=7.5, ec=C['red'])
    ar(ax, 8.45, 3.1, 8.45, 2.75, color=C['red'])
    ax.text(8.6, 2.9, '是', fontsize=7, color=C['red'])
    bx(ax, 7.2, 2.6, 2.5, 0.3, "NO: INVISIBLE\n(已被删除)", face='#EF9A9A',
       size=7.5, ec='#B71C1C')

    ar(ax, 8.45, 2.6, 8.45, 2.3, color='#333')
    bx(ax, 7.2, 2.0, 2.5, 0.35, "IsActive(txn_id) ?", face='#FFCDD2',
       size=8, ec=C['red'])
    ar(ax, 8.45, 2.0, 8.45, 1.65, color=C['red'])
    ax.text(8.6, 1.8, '是', fontsize=7, color=C['red'])
    bx(ax, 7.2, 1.5, 2.5, 0.3, "NO: INVISIBLE\n(事务未提交)", face='#EF9A9A',
       size=7.5, ec='#B71C1C')

    bx(ax, 5, 1.0, 4, 0.5, "YES: VISIBLE (全部检查通过)",
       face='#A5D6A7', size=9, weight='bold', ec='#1B5E20')

    save(fig)

# ====== 图3: MiniDB 架构 ======
def d3_arch():
    fig = plt.figure(figsize=(12, 7), facecolor=C['bg'])
    fig._label = "minidb_arch"
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 12); ax.set_ylim(0, 7); ax.axis('off')

    ax.text(6, 6.7, "MiniDB MVCC 架构总览", ha='center', fontsize=14, fontweight='bold')

    bx(ax, 3.5, 5.8, 5, 0.6, "REPL + SQL 解析器 (main.cpp / parser.cpp)",
       face='#E8EAF6', size=10, weight='bold', ec='#303F9F')

    bx(ax, 1, 4.5, 10, 1.0, "", face='#E8F5E9', ec=C['minidb'])
    ax.text(6, 5.2, "MiniDB (database.cpp)", ha='center', fontsize=10,
            weight='bold', color=C['minidb'])
    bx(ax, 1.5, 4.6, 2, 0.3, "BeginTxn()",  face='white', size=7.5, ec='#999')
    bx(ax, 4.2, 4.6, 2, 0.3, "CommitTxn()", face='white', size=7.5, ec='#999')
    bx(ax, 6.9, 4.6, 2, 0.3, "AbortTxn()",  face='white', size=7.5, ec='#999')
    bx(ax, 9.5, 4.6, 1.2, 0.3, "Scan()",     face='white', size=7.5, ec='#999')

    ar(ax, 6, 4.5, 6, 4.05, color='#333')
    bx(ax, 2.5, 3.3, 7, 0.8, "", face='#FFF8E1', ec='#F57F17')
    ax.text(6, 3.85, "TransactionManager (transaction.cpp)", ha='center',
            fontsize=10, weight='bold', color='#F57F17')
    bx(ax, 2.8, 3.38, 2.2, 0.28, "next_txn_id_ 递增", face='white', size=7, ec='#999')
    bx(ax, 5.4, 3.38, 2.2, 0.28, "next_ts_ 递增",     face='white', size=7, ec='#999')
    bx(ax, 8.0, 3.38, 1.2, 0.28, "IsVisible()",       face='white', size=7,
       ec=C['red'], ew=2)

    ar(ax, 3.5, 3.3, 3.5, 2.7, color='#333')
    ar(ax, 6, 3.3, 6, 2.7, color='#333')
    ar(ax, 8.5, 3.3, 8.5, 2.7, color='#333')

    bx(ax, 1.5, 2.0, 4, 0.7, "Transaction\n- txn_id_ / read_ts_ / commit_ts_",
       face=C['undo'], size=8, ec='#1976D2')
    bx(ax, 4.5, 2.0, 3, 0.7, "WriteRecord (Undo Log)\n- rid / old_end_ts / is_insert",
       face=C['undo'], size=8, ec='#1976D2')
    bx(ax, 7.3, 2.0, 3.2, 0.7, "TupleMeta (每行元数据)\n[txn_id][begin_ts][end_ts]",
       face=C['undo'], size=8, ec='#1976D2')

    ar(ax, 6, 2.0, 6, 1.45, color='#333')
    bx(ax, 2, 0.5, 8, 0.85, "", face=C['page'], ec='#E65100', ew=2)
    ax.text(6, 1.15, "Page [4096 bytes] — 存储在磁盘上", ha='center',
            fontsize=10, weight='bold')
    bx(ax, 2.3, 0.6, 1.8, 0.4, "Tuple 1", face='white', size=7, ec='#999')
    bx(ax, 4.6, 0.6, 1.8, 0.4, "Tuple 2", face='white', size=7, ec='#999')
    bx(ax, 6.9, 0.6, 1.8, 0.4, "Tuple 3", face='white', size=7, ec='#999')

    ar(ax, 3.2, 1.35, 3.2, 1.1, color='#E65100', lw=1.2)
    ar(ax, 5.5, 1.35, 5.5, 1.1, color='#E65100', lw=1.2)
    ar(ax, 7.8, 1.35, 7.8, 1.1, color='#E65100', lw=1.2)

    save(fig)

# ====== 图4: 事务生命周期 ======
def d4_life():
    fig = plt.figure(figsize=(13, 7.5), facecolor=C['bg'])
    fig._label = "txn_lifecycle"
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 13); ax.set_ylim(0, 7.5); ax.axis('off')

    ax.text(6.5, 7.2, "MiniDB 事务生命周期时间线", ha='center',
            fontsize=14, fontweight='bold')

    ax.arrow(1, 6.0, 11, 0, head_width=0.15, head_length=0.3, fc='k', ec='k', lw=2)
    ax.text(12.5, 6.0, '时间', fontsize=9)

    times = [2, 4, 6, 8, 10, 12]
    labels = ['t1: BEGIN\n(read_ts=10)', 't2: 插入行A', 't3: 插入行B',
              't4: 删除行A', 't5: COMMIT\n(commit_ts=15)', 't6']
    for t, label in zip(times, labels):
        ax.plot(t, 6.0, 'ko', markersize=5)
        ax.text(t, 5.6, label, ha='center', fontsize=7, color='#333')

    bx(ax, 1.8, 4.8, 4.5, 0.7, "运行阶段\n读操作基于 read_ts=10",
       face='#E8F5E9', size=8.5, ec=C['green'])
    bx(ax, 7.5, 4.8, 3, 0.7, "提交阶段\n分配 commit_ts=15",
       face='#C8E6C9', size=8.5, ec=C['green'], weight='bold')

    bx(ax, 1, 3.5, 11, 1.0, "", face='#FFF3E0', ec='#F57F17')
    ax.text(6.5, 4.3, "WriteSet (Undo Log) 累积过程", ha='center',
            fontsize=10, weight='bold', color='#F57F17')

    ws = [(2, 3.6, "插入A\nrid=100", '#BBDEFB'),
          (5, 3.6, "插入B\nrid=101", '#BBDEFB'),
          (8, 3.6, "删除A\nrid=100", '#FFCDD2')]
    for x, y, text, color in ws:
        bx(ax, x, y, 2, 0.6, text, face=color, size=8, ec='#999')
        ar(ax, x+1, y+0.6, x+1, y+0.8, color='#F57F17', lw=0.8)

    bx(ax, 1, 1.8, 11, 1.3, "", face=C['page'], ec='#E65100')
    ax.text(6.5, 2.9, "磁盘上 TupleMeta 的变化", ha='center',
            fontsize=10, weight='bold', color='#E65100')

    ds = [(2, 2.0, "A: [0|TS_MAX]\n(未提交插入)", C['undo']),
          (5, 2.0, "B: [0|TS_MAX]\n(未提交插入)", C['undo']),
          (8, 2.0, "A: [0|0]\n(标记删除)", '#FFCDD2'),
          (10, 2.0, "A: [15|15]\nB: [15|MAX]\n(提交完成)", '#C8E6C9')]
    for x, y, text, color in ds:
        bx(ax, x, y, 2.5, 0.9, text, face=color, size=7, ec='#E65100')
        ar(ax, x+1.25, 2.9, x+1.25, 2.95, color='#E65100', lw=0.8)

    save(fig)

# ====== 图5: ReadView 决策树 ======
def d5_tree():
    fig = plt.figure(figsize=(10, 6.5), facecolor=C['bg'])
    fig._label = "readview_tree"
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 10); ax.set_ylim(0, 6.5); ax.axis('off')

    ax.text(5, 6.2, "MySQL ReadView 可见性决策树", ha='center',
            fontsize=14, fontweight='bold')

    bx(ax, 3.5, 5.3, 3, 0.5, "trx_id < min_trx_id ?",
       face=C['undo'], size=9, ec='#1976D2')
    ar(ax, 4, 5.3, 1.5, 4.8, color=C['green'], lw=1.5)
    ax.text(2.3, 5.0, '是', fontsize=7, color=C['green'])
    bx(ax, 0.5, 4.5, 2.2, 0.45, "YES: VISIBLE\n(快照创建前已提交)",
       face='#A5D6A7', size=8, ec='#1B5E20')

    ar(ax, 6.5, 5.05, 8.5, 4.8, color=C['red'], lw=1.5)
    ax.text(7.8, 4.9, '否', fontsize=7, color=C['red'])
    bx(ax, 7.3, 4.5, 2.2, 0.5, "trx_id >= max_trx_id ?",
       face='#FFCDD2', size=8.5, ec=C['red'])

    ar(ax, 8.4, 4.5, 8.4, 4.0, color=C['green'], lw=1.5)
    ax.text(7.9, 4.2, '是', fontsize=7, color=C['green'])
    bx(ax, 7.3, 3.75, 2.2, 0.45, "NO: INVISIBLE\n(快照创建后才开始)",
       face='#EF9A9A', size=7.5, ec='#B71C1C')

    ar(ax, 8.4, 3.75, 8.4, 3.3, color='#333')
    bx(ax, 4.5, 3.0, 3.5, 0.5, "trx_id in m_ids ?",
       face='#FFF3E0', size=9, ec='#E65100')
    ar(ax, 3, 2.3, 2, 2.3, color='#333')
    ax.text(2.5, 3.1, "(min <= trx_id < max)", fontsize=7, color='#666')

    ar(ax, 5.5, 3.0, 2, 2.5, color=C['red'], lw=1.5)
    ax.text(3.5, 2.7, '是\n(在列表中)', fontsize=6.5, color=C['red'], ha='center')
    bx(ax, 0.5, 2.2, 2.2, 0.45, "NO: INVISIBLE\n(尚未提交)",
       face='#EF9A9A', size=8, ec='#B71C1C')

    ar(ax, 8, 3.0, 8, 2.5, color=C['green'], lw=1.5)
    ax.text(7.5, 2.7, '否\n(不在列表中)', fontsize=6.5, color=C['green'], ha='center')
    bx(ax, 6.8, 2.2, 2.4, 0.45, "YES: VISIBLE\n(快照创建前已提交)",
       face='#A5D6A7', size=7.5, ec='#1B5E20')

    bx(ax, 2, 1.0, 6, 0.7,
       "min_trx_id = ReadView 创建时最小活跃 trx_id\n"
       "max_trx_id = 即将分配的下一个 trx_id (最长活跃 id + 1)\n"
       "m_ids = ReadView 创建时所有活跃 trx_id 的集合",
       face='#FAFAFA', size=7.5, ec='#CCC')

    save(fig)

# ====== 图6: 四种数据库对比 ======
def d6_cmp():
    fig = plt.figure(figsize=(14, 5.5), facecolor=C['bg'])
    fig._label = "mvcc_comparison"
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 14); ax.set_ylim(0, 5.5); ax.axis('off')

    ax.text(7, 5.2, "四种 MVCC 实现对比", ha='center', fontsize=14, fontweight='bold')

    x_pos = [0, 1.0, 3.9, 6.8, 9.7]
    h_colors = [C['grey'], C['mysql'], C['pg'], C['rocks'], C['minidb']]
    headers = ["", "MySQL InnoDB", "PostgreSQL", "RocksDB", "MiniDB"]

    for i in range(1, 5):
        bx(ax, x_pos[i]+0.1, 4.6, 2.6, 0.4, headers[i], face=h_colors[i],
           size=9.5, tcolor='white', weight='bold', ec='none')

    rows = [
        ("存储方式", "原地更新\n+ Undo Log", "追加写\n(out-of-place)",
         "LSM-Tree\n(SST 文件)", "原地更新\nTupleMeta"),
        ("版本标记", "roll_ptr 链", "xmin / xmax", "序号\n(Sequence)", "begin_ts /\nend_ts"),
        ("可见性判断", "ReadView\n+ m_ids", "xmin/xmax\n+ clog",
         "序号快照", "read_ts\n+ IsActive"),
        ("清理机制", "purge 线程", "VACUUM", "Compaction", "原地覆盖\n(无需清理)"),
    ]

    for r, (label, *vals) in enumerate(rows):
        y = 3.8 - r * 0.75
        bx(ax, x_pos[0], y, 1.8, 0.55, label, face='#ECEFF1',
           size=8.5, weight='bold', ec='#999')
        for c, val in enumerate(vals):
            bx(ax, x_pos[c+1]+0.3, y, 2.4, 0.55, val, face='white',
               size=7.5, ec=h_colors[c+1], ew=1.3)

    save(fig)

# ====== Main ======
if __name__ == "__main__":
    print("Generating diagrams (Chinese labels)...")
    d1_mysql()
    d2_vis()
    d3_arch()
    d4_life()
    d5_tree()
    d6_cmp()
    print(f"\n6 diagrams saved to {OUT}/")
