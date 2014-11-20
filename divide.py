#!/usr/bin/python3.2

""" Code from http://xxyxyz.org/line-breaking/ with minor modifications """

def divide(words, width, wordlen):
    count = len(words)
    offsets = [0]
    for w in words:
        offsets.append(offsets[-1] + wordlen(w))

    minima = [0] + [10 ** 20] * count
    breaks = [0] * (count + 1)

    def cost(i, j):
        w = offsets[j] - offsets[i] + j - i - 1
        if w > width:
            return 10 ** 10
        return minima[i] + (width - w) ** 2

    def search(i0, j0, i1, j1):
        stack = [(i0, j0, i1, j1)]
        while stack:
            i0, j0, i1, j1 = stack.pop()
            if j0 < j1:
                j = (j0 + j1) // 2
                for i in range(i0, i1):
                    c = cost(i, j)
                    if c <= minima[j]:
                        minima[j] = c
                        breaks[j] = i
                stack.append((breaks[j], j+1, i1, j1))
                stack.append((i0, j0, breaks[j]+1, j))

    n = count + 1
    i = 0
    offset = 0
    while True:
        r = min(n, 2 ** (i + 1))
        edge = 2 ** i + offset
        search(0 + offset, edge, edge, r + offset)
        x = minima[r - 1 + offset]
        for j in range(2 ** i, r - 1):
            y = cost(j + offset, r - 1 + offset)
            if y <= x:
                n -= j
                i = 0
                offset += j
                break
        else:
            if r == n:
                break
            i = i + 1

    lines = []
    j = count
    while j > 0:
        i = breaks[j]
        #lines.append(' '.join(words[i:j]))
        lines.append(words[i:j])
        j = i
    lines.reverse()
    return lines

def linear(text, width, wordlen):
    words = text.split()
    count = len(words)
    offsets = [0]
    for w in words:
        offsets.append(offsets[-1] + wordlen(w))

    minima = [0] + [10 ** 20] * count
    breaks = [0] * (count + 1)

    def cost(i, j):
        w = offsets[j] - offsets[i] + j - i - 1
        if w > width:
            return 10 ** 10 * (w - width)
        return minima[i] + (width - w) ** 2

    def smawk(rows, columns):
        stack = []
        i = 0
        while i < len(rows):
            if stack:
                c = columns[len(stack) - 1]
                if cost(stack[-1], c) < cost(rows[i], c):
                    if len(stack) < len(columns):
                        stack.append(rows[i])
                    i += 1
                else:
                    stack.pop()
            else:
                stack.append(rows[i])
                i += 1
        rows = stack

        if len(columns) > 1:
            smawk(rows, columns[1::2])

        i = j = 0
        while j < len(columns):
            if j + 1 < len(columns):
                end = breaks[columns[j + 1]]
            else:
                end = rows[-1]
            c = cost(rows[i], columns[j])
            if c < minima[columns[j]]:
                minima[columns[j]] = c
                breaks[columns[j]] = rows[i]
            if rows[i] < end:
                i += 1
            else:
                j += 2

    n = count + 1
    i = 0
    offset = 0
    while True:
        r = min(n, 2 ** (i + 1))
        edge = 2 ** i + offset
        smawk(range(0 + offset, edge), range(edge, r + offset))
        x = minima[r - 1 + offset]
        for j in range(2 ** i, r - 1):
            y = cost(j + offset, r - 1 + offset)
            if y <= x:
                n -= j
                i = 0
                offset += j
                break
        else:
            if r == n:
                break
            i = i + 1

    lines = []
    j = count
    while j > 0:
        i = breaks[j]
        #lines.append(' '.join(words[i:j]))
        lines.append(join(words[i:j]))
        j = i
    lines.reverse()
    return lines
