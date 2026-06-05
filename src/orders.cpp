#include "pch.h"


void Order::PartialUpdate(Quantity quantityfilled){
    //condition ? expression_if_true : expression_if_false;
    qty = (quantityfilled >= qty) ? 0 : (qty - quantityfilledq);
}

