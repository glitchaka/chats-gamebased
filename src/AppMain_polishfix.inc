void drawChamferedBox(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int cut = 8) {
    POINT points[8] = {
        {rect.left+cut,rect.top},{rect.right-cut,rect.top},{rect.right,rect.top+cut},{rect.right,rect.bottom-cut},
        {rect.right-cut,rect.bottom},{rect.left+cut,rect.bottom},{rect.left,rect.bottom-cut},{rect.left,rect.top+cut}
    };
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, points, 8);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}
