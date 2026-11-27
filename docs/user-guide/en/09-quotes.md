<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# 9. Quotes

The **Quote** tab (or **Project ▸ Quote**) is a customer price estimate calculated only from the **library prices**
and the **pricing settings**. Nothing is estimated: an item without a price is shown as **UNPRICED** and is not
included in the total.

## Before the first quote

1. Enter prices in **Project ▸ Libraries…** ([chapter 4](04-materials-and-libraries.md)):
   materials (**Cost per m²** and/or **Cost per sheet**), edge bands (**Cost per metre**), hardware (**Unit cost**).
   Glass panes are priced by the glass material's cost per m².
2. Open **Project ▸ Pricing settings…** and set:
   * **Waste** (% added to the cut area of boards priced by area),
   * **Markup** (% on the subtotal) and **Tax** (% on subtotal + markup),
   * labour rates: **Labour per part**, **Labour per drilled hole**, **Labour per edge banding metre**, **Labour per
     cut metre**,
   * **Currency** (a label only, no conversion).

## Reading the quote

Lines are grouped: materials, edge banding, hardware, bought-in items, labour, then subtotal, markup, tax and
**TOTAL**.

* **Use cutting plan sheets** off: boards are priced by cut area + waste.
* **Use cutting plan sheets** on: boards are priced per sheet from the (heuristic) cutting plans; the cut length of
  the plans feeds "labour per cut metre" (without plans that labour line is unpriced).

**Export CSV…** saves the quote for a spreadsheet or your own quote template.

## Limits

No rounding to purchasable units (e.g. whole rolls), no per-material waste, no discounts. Check every quote before
sending it. A batch quote of several projects is available from the command line only: `fos batch quote`.
