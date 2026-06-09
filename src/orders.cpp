#include "pch.h"


void Order::PartialUpdate(Quantity quantityfilled){
    //condition ? expression_if_true : expression_if_false;
    qty = (quantityfilled >= qty) ? 0 : (qty - quantityfilled);
    if (qty == 0){
        OrderBook.cancel(id)
    }
}

//if quantity = 0, delete the order

void Order::modifyOrder(){


}


//streamlit